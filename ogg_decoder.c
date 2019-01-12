#include "ogg_decoder.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "ogg/vorbis/vorbisenc.h"

struct _OGGDecoder {
	ogg_sync_state oy; /* sync and verify incoming physical bitstream */
	ogg_stream_state os; /* take physical pages, weld into a logical stream of packets */
	ogg_page og; /* one Ogg bitstream page. Vorbis packets are inside */
	ogg_packet op; /* one raw packet of data for decode */

	vorbis_info vi; /* struct that stores all the static vorbis bitstream settings */
	vorbis_comment vc; /* struct that stores all the bitstream user comments */
	vorbis_dsp_state vd; /* central working state for the packet->PCM decoder */
	vorbis_block vb; /* local working space for packet->PCM decode */

	int16_t convBuffer[4096];

	int state;
	FramesOutputCallback dataCallback;
	void* userData;
};

OGGDecoder* OGGDecoder_new(FramesOutputCallback dataCallback, void* userData) {
	OGGDecoder* state = malloc(sizeof(OGGDecoder));
	memset(state, 0, sizeof(OGGDecoder));

	ogg_sync_init(&state->oy);

	state->state = 0;

	state->dataCallback = dataCallback;
	state->userData = userData;

	vorbis_info_init(&state->vi);
	vorbis_comment_init(&state->vc);

	return state;
}

void OGGDecoder_init(OGGDecoder* state) {
}

void OGGDecoder_free(OGGDecoder* state) {
	vorbis_block_clear(&state->vb);
	vorbis_dsp_clear(&state->vd);

	ogg_stream_clear(&state->os);
	vorbis_comment_clear(&state->vc);
	vorbis_info_clear(&state->vi);
	ogg_sync_clear(&state->oy);

	free(state);
}

int OGGDecoder_getSampleRate(OGGDecoder* state) {
	return (int)state->vi.rate;
}

int OGGDecoder_getChannelsCount(OGGDecoder* state) {
	return state->vi.channels;
}

int OGGDecoder_processDataInternal(OGGDecoder* state, void* data, int length) {
	char* buffer = ogg_sync_buffer(&state->oy, length);
	memcpy(buffer, data, (size_t)length);

	ogg_sync_wrote(&state->oy, length);

	check:
	if (state->state == 0) {
		if (ogg_sync_pageout(&state->oy, &state->og) != 1) {
			/* have we simply run out of data?  If so, we're done. */
			if (length < 4096)
				return 0;

			/* error case.  Must not be Vorbis data */
			fprintf(stderr, "Input does not appear to be an Ogg bitstream.\n");
			return 1;
		}

		ogg_stream_init(&state->os, ogg_page_serialno(&state->og));

		if (ogg_stream_pagein(&state->os, &state->og) < 0) {
			/* error; stream version mismatch perhaps */
			fprintf(stderr, "Error reading first page of Ogg bitstream data.\n");
			return 1;
		}

		if (ogg_stream_packetout(&state->os, &state->op) != 1) {
			/* no page? must not be vorbis */
			fprintf(stderr, "Error reading initial header packet.\n");
			return 1;
		}

		if (vorbis_synthesis_headerin(&state->vi, &state->vc, &state->op) < 0) {
			/* error case; not a vorbis header */
			fprintf(stderr, "This Ogg bitstream does not contain Vorbis audio data.\n");
			return 1;
		}

		state->state = 1;
	}

	if (state->state == 1 || state->state == 2) {
		int result = ogg_sync_pageout(&state->oy, &state->og);
		if (result == 0) {
			return 0; /* Need more data */
		}

		if (result == 1) {
			/* we can ignore any errors here as they'll also become apparent at packetout */
			ogg_stream_pagein(&state->os, &state->og);

			result = ogg_stream_packetout(&state->os, &state->op);
			if (result == 0)
				return 0;
			if (result < 0) {
				/* Uh oh; data at some point was corrupted or missing! We can't tolerate that in a header. Die. */
				fprintf(stderr, "Corrupt secondary header.  Exiting.\n");
				return 1;
			}

			result = vorbis_synthesis_headerin(&state->vi, &state->vc, &state->op);
			if (result < 0) {
				fprintf(stderr, "Corrupt secondary header.  Exiting.\n");
				return 1;
			}

			state->state++;
			goto check;
		}
	}

	if (state->state == 3) {
		fprintf(stderr, "\nBitstream is %d channel, %ldHz\n", state->vi.channels, state->vi.rate);
		fprintf(stderr, "Encoded by: %s\n\n", state->vc.vendor);


		if (vorbis_synthesis_init(&state->vd, &state->vi) != 0) {
			fprintf(stderr, "Error: Corrupt header during playback initialization.\n");
			return 1;
		}

		vorbis_block_init(&state->vd, &state->vb);

		state->state = 4;
	}

	if (state->state == 4) {
		/* OK, got and parsed all three headers. Initialize the Vorbis
		   packet->PCM decoder. */

		/* The rest is just a straight decode loop until end of stream */

		int result = ogg_sync_pageout(&state->oy, &state->og);
		if (result == 0)
			return 0; /* need more data */

		if (result < 0) { /* missing or corrupt data at this page position */
			fprintf(stderr, "Corrupt or missing data in bitstream; continuing...\n");
			return 1;
		}

		ogg_stream_pagein(&state->os, &state->og); /* can safely ignore errors at this point */
		while (1) {
			result = ogg_stream_packetout(&state->os, &state->op);

			if (result == 0)
				break; /* need more data */

			/* we have a packet. Decode it */

			if (vorbis_synthesis(&state->vb, &state->op) == 0) /* test for success! */
				vorbis_synthesis_blockin(&state->vd, &state->vb);

			/*
			**pcm is a multichannel float vector. In stereo, for
			example, pcm[0] is left, and pcm[1] is right. samples is
			the size of each channel. Convert the float values
			(-1.<=range<=1.) to whatever PCM format and write it out */

			float** pcm;
			int samples;
			int maxConvSize = 4096 / state->vi.channels;

			while ((samples = vorbis_synthesis_pcmout(&state->vd, &pcm)) > 0) {
				int bout = (samples < maxConvSize ? samples : maxConvSize);

				/* convert floats to 16 bit signed ints (host order) and interleave */
				for (int i = 0; i < state->vi.channels; i++) {
					int16_t* ptr = state->convBuffer + i;
					float* mono = pcm[i];
					for (int j = 0; j < bout; j++) {
						int val = (int)floorf(mono[j] * 32767.f + .5f);

						if (val > 32767) val = 32767;
						if (val < -32768) val = -32768;

						*ptr = (int16_t)val;
						ptr += state->vi.channels;
					}
				}

				state->dataCallback(state->convBuffer, bout, state->userData);

				vorbis_synthesis_read(&state->vd, bout); /* tell libvorbis how many samples we actually consumed */
			}
		}

		if (ogg_page_eos(&state->og)) {
			vorbis_block_clear(&state->vb);
			vorbis_dsp_clear(&state->vd);

			vorbis_block_init(&state->vd, &state->vb);
		}
	}

	return 0;
}

int OGGDecoder_writeData(OGGDecoder* state, void* data, int length) {
	int pos = 0;

	while (pos != length) {
		int toRead = length - pos;
		if (toRead > 4096)
			toRead = 4096;

		int res = OGGDecoder_processDataInternal(state, data, toRead);
		if (res != 0)
			return res;

		data += toRead;
		pos += toRead;
	}

	return 0;
}

void OGGDecoder_finalize(OGGDecoder* state) {
	vorbis_block_clear(&state->vb);
	vorbis_dsp_clear(&state->vd);
}