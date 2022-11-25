#pragma once
#define GAME_API_BASE 500

// System calls written in assembly
#define ECALL_SELF_TEST      (GAME_API_BASE+0)
#define ECALL_ASSERT_FAIL    (GAME_API_BASE+1)
#define ECALL_WRITE          (GAME_API_BASE+2)
#define ECALL_MEASURE        (GAME_API_BASE+3)
#define ECALL_DYNCALL        (GAME_API_BASE+4)
#define ECALL_FARCALL        (GAME_API_BASE+5)
#define ECALL_FARCALL_DIRECT (GAME_API_BASE+6)
#define ECALL_INTERRUPT      (GAME_API_BASE+7)
#define ECALL_MACHINE_HASH   (GAME_API_BASE+8)
#define ECALL_EACH_FRAME     (GAME_API_BASE+9)
#define ECALL_MULTIPROCESS_FORK  (GAME_API_BASE+10)
#define ECALL_MULTIPROCESS_WAIT  (GAME_API_BASE+11)
#define ECALL_GAME_EXIT      (GAME_API_BASE+12)

enum game_api_ids
{
	// Math
	ECALL_SINF = GAME_API_BASE+14,
	ECALL_RANDF,
	ECALL_SMOOTHSTEP,
	ECALL_VEC_LENGTH,
	ECALL_VEC_ROTATE,
	ECALL_VEC_NORMALIZE,

	ECALL_LAST
};


#define REASON_FRAME         10
#define REASON_CAMERA_DOLLY  12
#define REASON_DIALOGUE      22
