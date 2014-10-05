/*
|| This file is part of Pike. For copyright information see COPYRIGHT.
|| Pike is distributed under GPL, LGPL and MPL. See the file COPYING
|| for more information.
*/

#ifndef SSCANF_H
#define SSCANF_H

INT32 low_sscanf_pcharp(PCHARP input, ptrdiff_t len,
                        PCHARP format, ptrdiff_t format_len,
                        ptrdiff_t *chars_matched, int flags);

INT32 low_sscanf(struct pike_string *data, struct pike_string *format, INT32 flags);
void o_sscanf(INT32 args, INT32 flags);
PMOD_EXPORT void f_sscanf(INT32 args);
void f_sscanf_76(INT32 args);
void f___handle_sscanf_format(INT32 flags);

#endif
