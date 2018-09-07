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

#ifdef _DEBUG
#define EXIT_ERROR() __debugbreak()
#define PRINT_ERROR(...) printf(__VA_ARGS__)
#else
#define EXIT_ERROR() exit(1)
#define PRINT_ERROR(...) printf(__VA_ARGS__)
#endif

void *pFrameData = NULL;

static void decode(AVCodecContext *pCodecContext, AVFrame *pFrame, AVPacket *pPacket, const char *filename, slapFileWriter *pFileWriter, const size_t sizeX, const size_t sizeY)
{
  char filenameBuffer[1024];
  int ret;

  ret = avcodec_send_packet(pCodecContext, pPacket);

  if (ret < 0)
  {
    PRINT_ERROR("Error sending a packet for decoding\n");
    EXIT_ERROR();
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
      PRINT_ERROR("Error during decoding\n");
      EXIT_ERROR();
    }

    printf("saving frame %3d\n", pCodecContext->frame_number);
    fflush(stdout);

    snprintf(filenameBuffer, sizeof(filenameBuffer), "%s-%d.slap", filename, pCodecContext->frame_number);

    size_t sizeXY = sizeX * sizeY;
    slapMemmove(pFrameData, pFrame->data[0], sizeXY);
    slapMemmove((uint8_t *)pFrameData + sizeXY, pFrame->data[1], sizeXY >> 2);
    slapMemmove((uint8_t *)pFrameData + (sizeXY + (sizeXY >> 2)), pFrame->data[2], sizeXY >> 2);

    slapResult result = slapFileWriter_AddFrameYUV420(pFileWriter, pFrameData);
    
    if (result)
    {
      PRINT_ERROR("Failed to add frame to slap.\n");
      EXIT_ERROR();
    }
  }
}

int main(int argc, char **argv)
{
  const char *filename, *outfilename;

  if (argc <= 2)
  {
    PRINT_ERROR("Usage: %s <input file> <output file>\n", argv[0]);
    exit(0);
  }

  filename = argv[1];
  outfilename = argv[2];

  AVFormatContext *pFormatContext = avformat_alloc_context();

  if (!pFormatContext)
  {
    PRINT_ERROR("Could not create AVFormatContext.\n");
    EXIT_ERROR();
  }

  if (avformat_open_input(&pFormatContext, filename, NULL, NULL))
  {
    PRINT_ERROR("Could not open input.\n");
    EXIT_ERROR();
  }

  if (avformat_find_stream_info(pFormatContext, NULL))
  {
    PRINT_ERROR("Could not get stream info.\n");
    EXIT_ERROR();
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
        PRINT_ERROR("Codec allocation failure.\n");
        EXIT_ERROR();
      }

      if (avcodec_parameters_to_context(pCodecContext, pCodecParams))
      {
        PRINT_ERROR("Codec parameters couldn't be applied to codec context.\n");
        EXIT_ERROR();
      }

      if (avcodec_open2(pCodecContext, pCodec, NULL))
      {
        PRINT_ERROR("Codec couln't be opened.\n");
        EXIT_ERROR();
      }
      
      break;
    }
  }

  if (streamIndex < 0)
  {
    PRINT_ERROR("No video stream could be found.\n");
    EXIT_ERROR();
  }

  slapFileWriter *pFileWriter = slapCreateFileWriter(outfilename, pCodecParams->width, pCodecParams->height, 1);

  if (!pFileWriter)
  {
    PRINT_ERROR("Could not create slap file writer.\n");
    EXIT_ERROR();
  }

  pFrameData = malloc(pCodecParams->width * pCodecParams->height * 3 / 2);

  AVPacket *pPacket = av_packet_alloc();
  AVFrame *pFrame = av_frame_alloc();

  if (!pPacket || !pFrame)
  {
    PRINT_ERROR("Could not allocate Frame or Packet.\n");
    EXIT_ERROR();
  }

  while (av_read_frame(pFormatContext, pPacket) >= 0)
  {
    if (pPacket->stream_index != (int)streamIndex)
      continue;

    if (pPacket->size)
      decode(pCodecContext, pFrame, pPacket, outfilename, pFileWriter, pCodecParams->width, pCodecParams->height);
  }

  slapResult result = slapFinalizeFileWriter(pFileWriter);

  if (result != slapSuccess)
  {
    PRINT_ERROR("Failed to write slap file.\n");
    EXIT_ERROR();
  }

  slapDestroyFileWriter(&pFileWriter);

  return 0;
}
