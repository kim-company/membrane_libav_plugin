defmodule Membrane.LibAV.Demuxer.FilterTest do
  use ExUnit.Case
  import Membrane.Testing.Assertions
  import Membrane.ChildrenSpec

  @testfiles [
    {"test/data/safari.mp4", "aac"},
    {"/Users/dmorn/projects/video-taxi-pepe-demo/test/data/babylon-30s-talk.mp4", "aac"},
    {"/Users/dmorn/projects/video-taxi-pepe-demo/test/data/babylon-30s-talk.ogg", "opus"},
    {"/Users/dmorn/Downloads/multi-lang.mp4", "aac"}
  ]

  describe "demuxer" do
    for {path, codec_name} <- @testfiles do
      test "detects #{codec_name} in #{path}" do
        spec = [
          child(:source, %Membrane.File.Source{location: unquote(path)})
          |> child(:demuxer, Membrane.LibAV.Demuxer.Filter)
        ]

        pid = Membrane.Testing.Pipeline.start_link_supervised!(spec: spec)
        codec_name = unquote(codec_name)

        # NOTE some streams in the input contain multiple tracks.
        assert_pipeline_notified(
          pid,
          :demuxer,
          {:new_stream, %{codec_name: ^codec_name, stream_index: _}},
          1_000
        )

        :ok = Membrane.Testing.Pipeline.terminate(pid)
      end
    end

    @tag :tmp_dir
    @tag skip: true
    test "aac data can be extracted from quicktime files", %{tmp_dir: tmp_dir} do
      output_path = Path.join([tmp_dir, "output.aac"])

      opts = [
        module: Support.Pipeline,
        custom_args: [
          source_path: "test/data/safari.mp4",
          decoder: {Membrane.AAC.FDK.Decoder, "aac"},
          output_path: output_path
        ]
      ]

      pid = Membrane.Testing.Pipeline.start_link_supervised!(opts)

      assert_end_of_stream(pid, :sink, :input, 2_000)
    end
  end
end
