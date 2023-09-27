defmodule Membrane.LibAV.PipelineTest do
  use ExUnit.Case
  import Membrane.Testing.Assertions
  import Membrane.ChildrenSpec

  defmodule Pipeline do
    use Membrane.Pipeline

    # @input_path "/Users/dmorn/Downloads/multi-lang.mp4"
    @input_path "/Users/dmorn/projects/video-taxi-pepe-demo/test/data/babylon-30s-talk.mp4"
    # @input_path "test/data/safari.mp4"
    # @input_path "/Users/dmorn/projects/video-taxi-pepe-demo/test/data/babylon-30s-talk.ogg"

    def handle_init(_ctx, opts) do
      spec = [
        child(:source, %Membrane.File.Source{location: @input_path})
        |> child(:demuxer, Membrane.LibAV.Demuxer)
      ]

      {[spec: spec], %{output_path: opts[:output_path], has_stream: false}}
    end

    def handle_child_notification(
          {:new_stream, stream = %{codec_name: "aac"}},
          :demuxer,
          _ctx,
          state = %{has_stream: false}
        ) do
      spec =
        get_child(:demuxer)
        |> via_out(Pad.ref(:output, stream.stream_index))
        |> child(:decoder, %Membrane.LibAV.Decoder{
          stream: stream
        })
        |> child(:converter, %Membrane.FFmpeg.SWResample.Converter{
          output_stream_format: %Membrane.RawAudio{
            channels: 1,
            sample_format: :s16le,
            sample_rate: 48_000
          }
        })
        |> child(:encoder, %Membrane.AAC.FDK.Encoder{
          aot: :mpeg4_he,
          bitrate_mode: 0
        })
        |> child(:sink, %Membrane.File.Sink{location: state.output_path})

      {[spec: spec], %{state | has_stream: true}}
    end

    def handle_child_notification(_notification, _child, _ctx, state) do
      {[], state}
    end
  end

  @tag :tmp_dir
  test "all©", %{tmp_dir: dir} do
    output = Path.join([dir, "output.aac"])

    pid =
      Membrane.Testing.Pipeline.start_link_supervised!(
        module: Pipeline,
        custom_args: [output_path: output]
      )

    assert_end_of_stream(pid, :sink, :input, 100_000)
    :ok = Membrane.Pipeline.terminate(pid)
  end
end
