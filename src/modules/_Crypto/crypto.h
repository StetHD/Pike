/*
 * $Id: crypto.h,v 1.2 1997/11/16 22:25:42 nisse Exp $
 *
 * Prototypes for some functions.
 *
 */

extern void assert_is_crypto_module(struct object *);

extern void pike_md5_init(void);
extern void pike_md5_exit(void);
extern void pike_crypto_init(void);
extern void pike_crypto_exit(void);
extern void pike_idea_init(void);
extern void pike_idea_exit(void);
extern void pike_des_init(void);
extern void pike_des_exit(void);
extern void pike_cast_init(void);
extern void pike_cast_exit(void);
extern void pike_rc4_init(void);
extern void pike_rc4_exit(void);
extern void pike_invert_init(void);
extern void pike_invert_exit(void);
extern void pike_sha_init(void);
extern void pike_sha_exit(void);
extern void pike_cbc_init(void);
extern void pike_cbc_exit(void);
extern void pike_pipe_init(void);
extern void pike_pipe_exit(void);

