defmodule Membrane.LibAV.DemuxerTest do
  use ExUnit.Case
  import Membrane.Testing.Assertions
  import Membrane.ChildrenSpec

  defp data_path(file) do
    Path.join(["test/data", file])
  end

  describe "demuxer" do
    test "finds tracks" do
      spec = [
        child(:source, %Membrane.File.Source{location: data_path("safari.mp4")})
        |> child(:demuxer, Membrane.LibAV.Demuxer)
      ]

      pid = Membrane.Testing.Pipeline.start_link_supervised!(spec: spec)

      assert_pipeline_notified(
        pid,
        :demuxer,
        {:new_stream, %{codec_name: "aac", stream_index: 0}},
        1_000
      )

      :ok = Membrane.Testing.Pipeline.terminate(pid)
    end
  end
end
