// see decode_video.c ffmpeg example.

#pragma warning(push, 0)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "slapcodec.h"
#pragma warning(pop)

#define INBUF_SIZE 4096

static void decode(AVCodecContext *pCodecContext, AVFrame *pFrame, AVPacket *pPacket, const char *filename, slapFileWriter *pFileWriter)
{
  char filenameBuffer[1024];
  int ret;

  ret = avcodec_send_packet(pCodecContext, pPacket);

  if (ret < 0)
  {
    fprintf(stderr, "Error sending a packet for decoding\n");
    exit(1);
  }

  while (ret >= 0)
  {
    ret = avcodec_receive_frame(pCodecContext, pFrame);

    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
    {
      return;
    }
    else if (ret < 0)
    {
      fprintf(stderr, "Error during decoding\n");
      exit(1);
    }

    printf("saving frame %3d\n", pCodecContext->frame_number);
    fflush(stdout);

    // the picture is allocated by the decoder. no need to free it
    snprintf(filenameBuffer, sizeof(filenameBuffer), "%s-%d.jpg", filename, pCodecContext->frame_number);

    // do something with the yuv image.
    // stbi_write_jpg(buf, pFrame->width, pFrame->height, 1, pFrame->data[0], 80);
    if (slapFileWriter_AddFrameYUV420(pFileWriter, pFrame->data[0], pFrame->linesize[0]))
    {
      fprintf(stderr, "Failed to add frame to slap.\n");
      exit(1);
    }
  }
}

int main(int argc, char **argv)
{
  const char *filename, *outfilename;

  if (argc <= 2)
  {
    fprintf(stderr, "Usage: %s <input file> <output file>\n", argv[0]);
    exit(0);
  }

  filename = argv[1];
  outfilename = argv[2];

  AVFormatContext *pFormatContext = avformat_alloc_context();

  if (!pFormatContext)
  {
    fprintf(stderr, "Could not create AVFormatContext.\n");
    exit(1);
  }

  if (avformat_open_input(&pFormatContext, filename, NULL, NULL))
  {
    fprintf(stderr, "Could not open input.\n");
    exit(1);
  }

  if (avformat_find_stream_info(pFormatContext, NULL))
  {
    fprintf(stderr, "Could not get stream info.\n");
    exit(1);
  }

  int streamIndex = -1;
  AVCodecContext *pCodecContext = NULL;
  AVCodec *pCodec = NULL;
  AVCodecParameters *pCodecParams = NULL;

  for (uint32_t i = 0; i < pFormatContext->nb_streams; i++)
  {
    pCodecParams = pFormatContext->streams[i]->codecpar;

    pCodec = avcodec_find_decoder(pCodecParams->codec_id);

    if (pCodecParams->codec_type == AVMEDIA_TYPE_VIDEO)
    {
      printf("Resolution: %dx%d\n", pCodecParams->width, pCodecParams->height);

      streamIndex = i;
      pCodecContext = avcodec_alloc_context3(pCodec);

      if (!pCodecContext)
      {
        fprintf(stderr, "Codec allocation failure.\n");
        exit(1);
      }

      if (avcodec_parameters_to_context(pCodecContext, pCodecParams))
      {
        fprintf(stderr, "Codec parameters couldn't be applied to codec context.\n");
        exit(1);
      }

      if (avcodec_open2(pCodecContext, pCodec, NULL))
      {
        fprintf(stderr, "Codec couln't be opened.\n");
        exit(1);
      }
      
      break;
    }
  }

  if (streamIndex < 0)
  {
    fprintf(stderr, "No video stream could be found.\n");
    exit(1);
  }

  AVPacket *pPacket = av_packet_alloc();
  AVFrame *pFrame = av_frame_alloc();

  if (!pPacket || !pFrame)
  {
    fprintf(stderr, "Could not allocate Frame or Packet.\n");
    exit(1);
  }

  slapFileWriter *pFileWriter = slapInitFileWriter(outfilename, pCodecParams->width, pCodecParams->height, 1);

  if (!pFileWriter)
  {
    fprintf(stderr, "Could not create slap file writer.\n");
    exit(1);
  }

  while (av_read_frame(pFormatContext, pPacket) >= 0)
  {
    if (pPacket->stream_index != (int)streamIndex)
      continue;

    if (pPacket->size)
      decode(pCodecContext, pFrame, pPacket, outfilename, pFileWriter);
  }

  slapFinalizeFileWriter(pFileWriter);
  slapDestroyFileWriter(&pFileWriter);

  return 0;
}
