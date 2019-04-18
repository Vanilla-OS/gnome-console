/* kgx-terminal.c
 *
 * Copyright 2019 Zander Brown
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <glib/gi18n.h>
#include <vte/vte.h>
#define PCRE2_CODE_UNIT_WIDTH 0
#include <pcre2.h>

#include "rgba.h"

#include "kgx.h"
#include "kgx-config.h"
#include "kgx-terminal.h"
#include "kgx-enums.h"

/*       Regex adapted from TerminalWidget.vala in Pantheon Terminal       */

#define USERCHARS "-[:alnum:]"
#define USERCHARS_CLASS "[" USERCHARS "]"
#define PASSCHARS_CLASS "[-[:alnum:]\\Q,?;.:/!%$^*&~\"#'\\E]"
#define HOSTCHARS_CLASS "[-[:alnum:]]"
#define HOST HOSTCHARS_CLASS "+(\\." HOSTCHARS_CLASS "+)*"
#define PORT "(?:\\:[[:digit:]]{1,5})?"
#define PATHCHARS_CLASS "[-[:alnum:]\\Q_$.+!*,;:@&=?/~#%\\E]"
#define PATHTERM_CLASS "[^\\Q]'.}>) \t\r\n,\"\\E]"
#define SCHEME "(?:news:|telnet:|nntp:|file:\\/|https?:|ftps?:|sftp:|webcal:\n" \
               "|irc:|sftp:|ldaps?:|nfs:|smb:|rsync:|"                          \
               "ssh:|rlogin:|telnet:|git:\n"                                    \
               "|git\\+ssh:|bzr:|bzr\\+ssh:|svn:|svn\\"                         \
               "+ssh:|hg:|mailto:|magnet:)"
#define USERPASS USERCHARS_CLASS "+(?:" PASSCHARS_CLASS "+)?"
#define URLPATH "(?:(/" PATHCHARS_CLASS "+(?:[(]" PATHCHARS_CLASS "*[)])*" PATHCHARS_CLASS "*)*" PATHTERM_CLASS ")?"

#define N_LINK_REGEX 5
static const gchar* links[N_LINK_REGEX] = {
  SCHEME "//(?:" USERPASS "\\@)?" HOST PORT URLPATH,
  "(?:www|ftp)" HOSTCHARS_CLASS "*\\." HOST PORT URLPATH,
  "(?:callto:|h323:|sip:)" USERCHARS_CLASS "[" USERCHARS ".]*(?:" PORT "/[a-z0-9]+)?\\@" HOST,
  "(?:mailto:)?" USERCHARS_CLASS "[" USERCHARS ".]*\\@" HOSTCHARS_CLASS "+\\." HOST,
  "(?:news:|man:|info:)[[:alnum:]\\Q^_{|}~!\"#$%&'()*+,./;:=?`\\E]+"
};

/*       Regex adapted from TerminalWidget.vala in Pantheon Terminal       */

struct _KgxTerminal
{
  VteTerminal parent_instance;

  KgxTheme    theme;
  GActionMap *actions; 

  /* Hyperlinks */
  const char *current_url;
  int         match_id[N_LINK_REGEX];
};

G_DEFINE_TYPE (KgxTerminal, kgx_terminal, VTE_TYPE_TERMINAL)

enum {
	PROP_0,
	PROP_THEME,
	LAST_PROP
};

static GParamSpec *pspecs[LAST_PROP] = { NULL, };


void
kgx_terminal_set_theme (KgxTerminal *self,
                        KgxTheme     theme)
{
  GdkRGBA fg;
  GdkRGBA bg = (GdkRGBA) { 0.1, 0.1, 0.1, 0.96};

  // Workings of GDK_RGBA prevent this being static
  GdkRGBA palette[16] = {
    GDK_RGBA ("241f31"), // Black
    GDK_RGBA ("c01c28"), // Red
    GDK_RGBA ("2ec27e"), // Green
    GDK_RGBA ("f5c211"), // Yellow
    GDK_RGBA ("1e78e4"), // Blue
    GDK_RGBA ("9841bb"), // Magenta
    GDK_RGBA ("0ab9dc"), // Cyan
    GDK_RGBA ("c0bfbc"), // White
    GDK_RGBA ("5e5c64"), // Bright Black
    GDK_RGBA ("ed333b"), // Bright Red
    GDK_RGBA ("57e389"), // Bright Green
    GDK_RGBA ("f8e45c"), // Bright Yellow
    GDK_RGBA ("51a1ff"), // Bright Blue
    GDK_RGBA ("c061cb"), // Bright Magenta
    GDK_RGBA ("4fd2fd"), // Bright Cyan
    GDK_RGBA ("f6f5f4"), // Bright White
  };

  self->theme = theme;

  switch (theme) {
    case KGX_THEME_HACKER:
      fg = (GdkRGBA) { 0.1, 1.0, 0.1, 1.0};
      break;
    case KGX_THEME_NIGHT:
    default:
      fg = (GdkRGBA) { 1.0, 1.0, 1.0, 1.0};
      break;
  }

  vte_terminal_set_colors (VTE_TERMINAL (self), &fg, &bg, palette, 16);

  g_object_notify_by_pspec (G_OBJECT (self), pspecs[PROP_THEME]);
}

static void
kgx_terminal_set_property (GObject      *object,
                           guint         property_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  KgxTerminal *self = KGX_TERMINAL (object);

  switch (property_id) {
    case PROP_THEME:
      kgx_terminal_set_theme (self, g_value_get_enum (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
kgx_terminal_get_property (GObject    *object,
                           guint       property_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  KgxTerminal *self = KGX_TERMINAL (object);

  switch (property_id) {
    case PROP_THEME:
      g_value_set_enum (value, self->theme);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
context_menu (GtkWidget *widget, GdkEventButton *event)
{
  KgxTerminal    *self = KGX_TERMINAL (widget);
  GAction        *act;
  GtkWidget      *menu;
  GtkApplication *app;
  GMenu          *model;
  GdkRectangle    rect = {0, 0, 1, 1};
  gboolean        value;
  const char     *match;
  int             match_id;

  match = vte_terminal_match_check_event (VTE_TERMINAL (self),
                                          (GdkEvent *) event,
                                          &match_id);

  self->current_url = NULL;
  for (int i = 0; i < N_LINK_REGEX; i++) {
    if (self->match_id[i] == match_id) {
      self->current_url = match;
      break;
    }
  }
  value = self->current_url != NULL;

  act = g_action_map_lookup_action (G_ACTION_MAP (self->actions), "open-link");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (act), value);
  act = g_action_map_lookup_action (G_ACTION_MAP (self->actions), "copy-link");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (act), value);

  app = GTK_APPLICATION (g_application_get_default ());
  model = gtk_application_get_menu_by_id (app, "context-menu");

  if (event) {
    rect = (GdkRectangle) { event->x, event->y, 1, 1 };
  }

  menu = gtk_popover_new_from_model (widget, G_MENU_MODEL (model));
  gtk_popover_set_pointing_to (GTK_POPOVER (menu), &rect);
  gtk_popover_popup (GTK_POPOVER (menu));
}

static gboolean
kgx_terminal_popup_menu (GtkWidget *self)
{
  context_menu (self, NULL);
  return TRUE;
}

static gboolean
kgx_terminal_button_press_event (GtkWidget *self, GdkEventButton *event)
{
  if (gdk_event_triggers_context_menu ((GdkEvent *) event) &&
      event->type == GDK_BUTTON_PRESS)
    {
      context_menu (self, event);
      return TRUE;
    }

  return GTK_WIDGET_CLASS (kgx_terminal_parent_class)->button_press_event (self, event);
}

static void
kgx_terminal_class_init (KgxTerminalClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->set_property = kgx_terminal_set_property;
  object_class->get_property = kgx_terminal_get_property;

  widget_class->popup_menu = kgx_terminal_popup_menu;
  widget_class->button_press_event = kgx_terminal_button_press_event;

  pspecs[PROP_THEME] =
    g_param_spec_enum ("theme", "Theme", "Terminal theme",
                       KGX_TYPE_THEME, KGX_THEME_NIGHT,
                       G_PARAM_READWRITE);

  g_object_class_install_properties (object_class, LAST_PROP, pspecs);
}

static void
open_link_activated (GSimpleAction *action,
                     GVariant      *parameter,
                     gpointer       data)
{
  GError *error = NULL;
  KgxTerminal *self = KGX_TERMINAL (data);
  guint32 timestamp;

  timestamp = GDK_CURRENT_TIME;

  gtk_show_uri_on_window (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (self))),
                          self->current_url,
                          timestamp,
                          &error);

  if (error) {
    g_warning (_("Failed to open link %s"), error->message);
  }
}

static void
copy_link_activated (GSimpleAction *action,
                     GVariant      *parameter,
                     gpointer       data)
{
  GtkClipboard *cb;
  KgxTerminal *self = KGX_TERMINAL (data);

  cb = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);

  gtk_clipboard_set_text (cb, self->current_url, -1);
}

static void
copy_activated (GSimpleAction *action,
                 GVariant      *parameter,
                 gpointer       data)
{
  vte_terminal_copy_clipboard_format (VTE_TERMINAL (data), VTE_FORMAT_TEXT);
}

static void
got_text (GtkClipboard *clipboard,
          const gchar  *text,
          gpointer      data)
{
  // TODO: Check for sudo

  // HACK: Technically a race condition here
  vte_terminal_paste_clipboard (VTE_TERMINAL (data));
}

static void
paste_activated (GSimpleAction *action,
                 GVariant      *parameter,
                 gpointer       data)
{
  GtkClipboard *cb;

  cb = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);

  gtk_clipboard_request_text (cb, got_text, data);
}

static void
select_all_activated (GSimpleAction *action,
                      GVariant      *parameter,
                      gpointer       data)
{
  vte_terminal_select_all (VTE_TERMINAL (data));
}

static void
show_in_files_activated (GSimpleAction *action,
                         GVariant      *parameter,
                         gpointer       data)
{
  GDBusProxy      *proxy;
  GVariant        *retval;
  GVariantBuilder *builder;
  GError          *error = NULL;
  KgxTerminal     *term = KGX_TERMINAL (data);
  const gchar     *uri = NULL;
  const gchar     *method;

  uri = vte_terminal_get_current_file_uri (VTE_TERMINAL (term));
  method = "ShowItems";

  if (uri == NULL) {
    uri = vte_terminal_get_current_directory_uri (VTE_TERMINAL (term));
    method = "ShowFolders";
  }

  if (uri == NULL) {
    g_warning (_("win.show-in-files: no file"));
    return;
  }

  proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                         G_DBUS_PROXY_FLAGS_NONE,
                                         NULL,
                                         "org.freedesktop.FileManager1",
                                         "/org/freedesktop/FileManager1",
                                         "org.freedesktop.FileManager1",
                                         NULL, &error);

  if (!proxy) {
    g_warning (_("win.show-in-files: dbus connect failed %s"), error->message);
    return;
  }

  builder = g_variant_builder_new (G_VARIANT_TYPE ("as"));
  g_variant_builder_add (builder, "s", uri);

  retval = g_dbus_proxy_call_sync (proxy,
                                   method,
                                   g_variant_new ("(ass)",
                                                  builder,
                                                  ""),
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1, NULL, &error);

  g_variant_builder_unref (builder);
  g_object_unref (proxy);

  if (!retval) {
    g_warning (_("win.show-in-files: dbus call failed %s"), error->message);
    return;
  }

  g_variant_unref (retval);
}

static GActionEntry term_entries[] =
{
  { "open-link", open_link_activated, NULL, NULL, NULL },
  { "copy-link", copy_link_activated, NULL, NULL, NULL },
  { "copy", copy_activated, NULL, NULL, NULL },
  { "paste", paste_activated, NULL, NULL, NULL },
  { "select-all", select_all_activated, NULL, NULL, NULL },
  { "show-in-files", show_in_files_activated, NULL, NULL, NULL },
};

static void
selection_changed (KgxTerminal *self)
{
  GAction *act;

  act = g_action_map_lookup_action (self->actions, "copy");

  g_simple_action_set_enabled (G_SIMPLE_ACTION (act),
                               vte_terminal_get_has_selection (VTE_TERMINAL (self)));
}

static void
location_changed (KgxTerminal *self)
{
  GAction *act;
  gboolean value;

  act = g_action_map_lookup_action (self->actions, "show-in-files");

  value = vte_terminal_get_current_file_uri (VTE_TERMINAL (self)) ||
            vte_terminal_get_current_directory_uri (VTE_TERMINAL (self));

  g_simple_action_set_enabled (G_SIMPLE_ACTION (act), value);
}

static void
kgx_terminal_init (KgxTerminal *self)
{
  GAction *act;

  self->actions = G_ACTION_MAP (g_simple_action_group_new ());
  g_action_map_add_action_entries (self->actions,
                                   term_entries,
                                   G_N_ELEMENTS (term_entries),
                                   self);
  gtk_widget_insert_action_group (GTK_WIDGET (self),
                                  "term",
                                  G_ACTION_GROUP (self->actions));

  act = g_action_map_lookup_action (self->actions, "open-link");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (act), FALSE);
  act = g_action_map_lookup_action (self->actions, "copy-link");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (act), FALSE);
  act = g_action_map_lookup_action (self->actions, "copy");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (act), FALSE);
  act = g_action_map_lookup_action (self->actions, "show-in-files");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (act), FALSE);

  vte_terminal_search_set_wrap_around (VTE_TERMINAL (self), TRUE);

  g_signal_connect (self, "selection-changed",
                    G_CALLBACK (selection_changed), NULL);
  g_signal_connect (self, "current-directory-uri-changed",
                    G_CALLBACK (location_changed), NULL);
  g_signal_connect (self, "current-file-uri-changed",
                    G_CALLBACK (location_changed), NULL);

  for (int i = 0; i < N_LINK_REGEX; i++) {
    VteRegex *regex;
    GError *error = NULL;

    regex = vte_regex_new_for_match (links[i], -1, PCRE2_MULTILINE, &error);

    if (error) {
      g_warning (_("link regex failed: %s"), error->message);
      continue;
    }

    self->match_id[i] = vte_terminal_match_add_regex (VTE_TERMINAL (self),
                                                      regex,
                                                      0);

    vte_terminal_match_set_cursor_name (VTE_TERMINAL (self),
                                        self->match_id[i],
                                        "pointer");
  }
}