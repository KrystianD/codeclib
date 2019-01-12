#include "ogg_encoder.h"

#include <stdlib.h>

#include "ogg/vorbis/vorbisenc.h"

struct _OGGEncoder {
	ogg_stream_state os; /* take physical pages, weld into a logical stream of packets */
	ogg_page og; /* one Ogg bitstream page.  Vorbis packets are inside */
	ogg_packet op; /* one raw packet of data for decode */

	vorbis_info vi; /* struct that stores all the static vorbis bitstream settings */
	vorbis_comment vc; /* struct that stores all the user comments */
	vorbis_dsp_state vd; /* central working state for the packet->PCM decoder */
	vorbis_block vb; /* local working space for packet->PCM decode */

	int channels;
	BytesOutputCallback dataCallback;
	void* userData;
};

OGGEncoder* OGGEncoder_new(int sampleRate, int channels, int quality, BytesOutputCallback dataCallback, void* userData) {
	OGGEncoder* state = malloc(sizeof(OGGEncoder));
	vorbis_info_init(&state->vi);

	state->channels = channels;
	state->dataCallback = dataCallback;
	state->userData = userData;

	const float minQuality = -0.1f;
	const float maxQuality = 1.0f;

	float q = quality / 100.0f * (maxQuality - minQuality) + minQuality;

	if (vorbis_encode_init_vbr(&state->vi, channels, sampleRate, q) != 0) {
		vorbis_info_clear(&state->vi);
		return 0;
	}

	vorbis_comment_init(&state->vc);

	if (vorbis_analysis_init(&state->vd, &state->vi) != 0) {
		vorbis_info_clear(&state->vi);
		return 0;
	}

	vorbis_block_init(&state->vd, &state->vb);

	if (ogg_stream_init(&state->os, rand()) != 0) {
		vorbis_info_clear(&state->vi);
		return 0;
	}

	return state;
}

void OGGEncoder_init(OGGEncoder* state) {
	ogg_packet header;
	ogg_packet header_comm;
	ogg_packet header_code;

	vorbis_analysis_headerout(&state->vd, &state->vc, &header, &header_comm, &header_code);
	ogg_stream_packetin(&state->os, &header);
	ogg_stream_packetin(&state->os, &header_comm);
	ogg_stream_packetin(&state->os, &header_code);

	for (;;) {
		int result = ogg_stream_flush(&state->os, &state->og);
		if (result == 0)
			break;

		state->dataCallback(state->og.header, (int)state->og.header_len, state->userData);
		state->dataCallback(state->og.body, (int)state->og.body_len, state->userData);
	}
}

void OGGEncoder_free(OGGEncoder* state) {
	ogg_stream_clear(&state->os);
	vorbis_block_clear(&state->vb);
	vorbis_dsp_clear(&state->vd);
	vorbis_comment_clear(&state->vc);
	vorbis_info_clear(&state->vi);
	free(state);
}

void OGGEncoder_process(OGGEncoder* state) {
	while (vorbis_analysis_blockout(&state->vd, &state->vb) == 1) {
		vorbis_analysis(&state->vb, NULL);
		vorbis_bitrate_addblock(&state->vb);

		while (vorbis_bitrate_flushpacket(&state->vd, &state->op)) {
			ogg_stream_packetin(&state->os, &state->op);

			while (!ogg_page_eos(&state->og)) {
				int result = ogg_stream_pageout(&state->os, &state->og);
				if (result == 0)
					break;

				state->dataCallback(state->og.header, (int)state->og.header_len, state->userData);
				state->dataCallback(state->og.body, (int)state->og.body_len, state->userData);
			}
		}
	}
}

void OGGEncoder_writeFrames(OGGEncoder* state, int16_t framesSamples[], int framesCount) {
	float** buffer = vorbis_analysis_buffer(&state->vd, framesCount);

	/* uninterleave samples */
	for (int ch = 0; ch < state->channels; ch++)
		for (int i = 0; i < framesCount; i++)
			buffer[ch][i] = (float)framesSamples[i * state->channels + ch] / 32768.f;

	vorbis_analysis_wrote(&state->vd, framesCount);

	OGGEncoder_process(state);
}

void OGGEncoder_writeFrame(OGGEncoder* state, int16_t frameSamples[]) {
	OGGEncoder_writeFrames(state, frameSamples, state->channels);
}

void OGGEncoder_finalize(OGGEncoder* state) {
	vorbis_analysis_wrote(&state->vd, 0);

	OGGEncoder_process(state);
}