/* fp-vte-util.h
 *
 * Copyright 2019 Christian Hergert <chergert@redhat.com>
 *
 * Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
 * https://www.apache.org/licenses/LICENSE-2.0> or the MIT License
 * <LICENSE-MIT or https://opensource.org/licenses/MIT>, at your
 * option. This file may not be copied, modified, or distributed
 * except according to those terms.
 *
 * SPDX-License-Identifier: (MIT OR Apache-2.0)
 */

#pragma once

#include <vte/vte.h>

gchar       *fp_vte_guess_shell      (GCancellable         *cancellable,
                                      GError              **error);
void         fp_vte_pty_spawn_async  (VtePty               *pty,
                                      const gchar          *working_directory,
                                      const gchar * const  *argv,
                                      const gchar * const  *env,
                                      gint                  timeout,
                                      GCancellable         *cancellable,
                                      GAsyncReadyCallback   callback,
                                      gpointer              user_data);
GSubprocess *fp_vte_pty_spawn_finish (VtePty               *pty,
                                      GAsyncResult         *result,
                                      GError              **error);
