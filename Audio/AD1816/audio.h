// audio.h
//
// 1998 Jens Winkler <jwin@gmx.net>
//
// old api audio buffer header
//
// 	(taken from a Be Newsletter,
//   sorry I forgot which one)
//

#include "sound.h"
#include "audio_driver.h"

typedef struct
{
  int32 buffer_number;
  int32 subscriber_count;
  bigtime_t time;
  int32 reserved_1;
  int32 reserved_2;
  bigtime_t sample_clock;
} audio_buffer_header;
