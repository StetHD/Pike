/*
 * $Id: ia32.h,v 1.2 2001/07/20 15:49:00 grubba Exp $
 */

#define UPDATE_PC() do {						\
    INT32 tmp=PC;							\
    add_to_program(0xa1 /* mov $xxxxx, %eax */);			\
    ins_int((INT32)(&Pike_interpreter.frame_pointer), add_to_program);	\
									\
    add_to_program(0xc7); /* movl $xxxxx, yy%(eax) */			\
    add_to_program(0x40);						\
    add_to_program(OFFSETOF(pike_frame, pc));				\
    ins_int((INT32)tmp, add_to_program);				\
}while(0)

#define READ_INCR_BYTE(PC)	EXTRACT_UCHAR((PC)++)
