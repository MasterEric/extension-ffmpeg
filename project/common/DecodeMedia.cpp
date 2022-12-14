#include "DecodeVideo.h"

/**
 * @brief Decodes a single frame from the media stream.
 *        Will call the `audioCallback` when data arrives,
 *        and the `videoCallback` when a video frame is ready to render.
 *
 * @param context The FFmpegContext containing the stream to decode.
 * @return 0 if another frame is needed to decode.
 *         1 if a video frame was decoded.
 *         2 if an audio frame was decoded.
 *        -# if an error occurred.
 */
int FFmpeg_decodeFrame(FFmpegContext *context)
{
    AVPacket pkt;

    // Read from the context into the packet.
    int result = av_read_frame(context->avFormatCtx, &pkt);
    if (result < 0)
    {
        if (result == AVERROR_EOF)
        {
            // printf("[extension-ffmpeg] Reached end of file.\n");
            return result;
        }
        else
        {
            // Failed to read from the context.
            // printf("[extension-ffmpeg] Failed to read next packet.\n");
            return result;
        }
    }

    if (pkt.stream_index == context->videoStreamIndex)
    {
        // printf("[extension-ffmpeg] Packet contains a video frame.\n");

        if (context->videoCodecCtx == nullptr)
        {
            // printf("[extension-ffmpeg] Video codec context not initialized.\n");
            result = 0;
        }
        else
        {
            // Send the data packet to the decoder.
            // printf("[extension-ffmpeg] Sending packet to video decoder.\n");
            result = avcodec_send_packet(context->videoCodecCtx, &pkt);
            if (result < 0)
            {
                // Failed to send the packet to the decoder.
                // printf("[extension-ffmpeg] Failed to send packet to video decoder.\n");
            }
            else
            {
                // Receive the frame back from the decoder.
                AVFrame *videoFrame = av_frame_alloc();
                // printf("[extension-ffmpeg] Receiving video frame from decoder.\n");
                result = avcodec_receive_frame(context->videoCodecCtx, videoFrame);
                if (result < 0)
                {
                    // Failed to decode the packet.
                    // printf("[extension-ffmpeg] Failed to decode video packet.\n");
                }
                else
                {
                    // Attempt to queue the video frame.
                    // The decoding thread will pause if the video queue is full.
                    // printf("[extension-ffmpeg] Queuing video frame (%d / %d)...\n", context->videoFrameQueue->size, context->audioFrameQueue->size);

                    // If the audio queue is empty, we need to start dumping video frames
                    // so we can catch up in the decoding process.
                    bool forcePush = context->audioFrameQueue->size == 0;
                    FFmpegFrameQueue_push(context->videoFrameQueue, videoFrame, forcePush);

                    // printf("[extension-ffmpeg] Success.\n");
                    // Positive 1 = successful video frame.
                    result = 1;
                }
            }
        }
    }
    else if (pkt.stream_index == context->audioStreamIndex)
    {
        // printf("[extension-ffmpeg] Packet contains an audio frame\n");

        // Fetch the audio packet's timestamp for synchronizing the video stream.
        if (pkt.pts != AV_NOPTS_VALUE)
        {
            // NOTE: This does not account for how much of the buffer has been used.
            context->audioClock = pkt.pts;
        }

        if (context->audioCodecCtx == nullptr)
        {
            // printf("[extension-ffmpeg] Audio codec context not initialized.\n");
            result = 0;
        }
        else
        {
            // Send the data packet to the decoder.
            // printf("[extension-ffmpeg] Sending audio packet to decoder...\n");
            result = avcodec_send_packet(context->audioCodecCtx, &pkt);
            if (result < 0)
            {
                // Failed to send the packet to the decoder.
                // printf("[extension-ffmpeg] Failed to send packet to audio decoder.\n");
            }
            else
            {
                // Receive the frame back from the decoder.
                // printf("[extension-ffmpeg] Receiving audio frame from decoder...\n");
                AVFrame *audioFrame = av_frame_alloc();
                result = avcodec_receive_frame(context->audioCodecCtx, audioFrame);
                if (result < 0)
                {
                    // Failed to decode the packet.
                    // printf("[extension-ffmpeg] Failed to decode audio packet.\n");
                    return result;
                }
                else
                {
                    // Attempt to queue the audio frame.
                    // The decoding thread will pause if the audio queue is full.
                    // printf("[extension-ffmpeg] Queuing audio frame (%d / %d)...\n", context->videoFrameQueue->size, context->audioFrameQueue->size);

                    // If the video queue is empty, we need to start dumping audio frames
                    // so we can catch up in the decoding process.
                    bool forcePush = context->videoFrameQueue->size == 0;
                    FFmpegFrameQueue_push(context->audioFrameQueue, audioFrame, forcePush);

                    // printf("[extension-ffmpeg] Success.\n");
                    // Positive 2 = successful audio frame.
                    result = 2;
                }
            }
        }
    }
    else if (pkt.stream_index == context->subtitleStreamIndex)
    {
        // printf("[extension-ffmpeg] Packet contains a subtitle frame\n");
        result = 0;
    }
    else
    {
        // Unknown stream index for frame.
        printf("[extension-ffmpeg] Packet contains an unknown frame (maybe from an uninitialized stream?)\n");
        result = 0;
    }

    // Free the packet.
    av_packet_unref(&pkt);

    return result;
}

/**
 * @brief Continuously decodes frames from the media stream.
 *        Designed to be run in a separate thread.
 */
void FFmpeg_decode_thread(FFmpegContext *context)
{
    printf("[extension-ffmpeg] Decode thread started.\n");
    while (!context->quit)
    {
        int result = FFmpeg_decodeFrame(context);
        if (result == 0 || result == AVERROR(EAGAIN))
        {
            // Decode another frame.
            continue;
        }
        else if (result == 1)
        {
            // Successfully decoded a video frame.
            // printf("[extension-ffmpeg] Successfully decoded a video frame.\n");
        }
        else if (result == 2)
        {
            // Successfully decoded an audio frame.
            // printf("[extension-ffmpeg] Successfully decoded an audio frame.\n");
        }
        else if (result == AVERROR_EOF)
        {
            printf("[extension-ffmpeg] Successfully decoded all frames.\n");
            break;
        }
        else
        {
            // Failed to decode the frame.
            printf("[extension-ffmpeg] Failed to decode frame: %d\n", result);
            break;
        }
    }
    // printf("[extension-ffmpeg] Decode thread finished.\n");
}

/**
 * @brief Starts a thread to decode frames from the media stream.
 */
int FFmpeg_startDecodeThread(FFmpegContext *context)
{
    context->decodeThread = new std::thread(FFmpeg_decode_thread, context);

    return 0;
}

DEFINE_FUNC_1(hx_ffmpeg_start_decode_thread, context)
{
    FFmpegContext *contextPointer = FFmpegContext_unwrap(context);
    int result = FFmpeg_startDecodeThread(contextPointer);

    return alloc_int(result);
}

/**
 * @brief Stops the decoding thread.
 */
int FFmpeg_stopDecodeThread(FFmpegContext *context)
{
    context->quit = true;
    context->decodeThread->join();
    delete context->decodeThread;

    return 0;
}

DEFINE_FUNC_1(hx_ffmpeg_stop_decode_thread, context)
{
    FFmpegContext *contextPointer = FFmpegContext_unwrap(context);
    int result = FFmpeg_stopDecodeThread(contextPointer);

    return alloc_int(result);
}
