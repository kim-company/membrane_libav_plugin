defmodule Support.Pipeline do
  use Membrane.Pipeline

  def handle_init(_ctx, opts) do
    spec = [
      child(:source, %Membrane.File.Source{location: opts[:source_path]})
      |> child(:demuxer, Membrane.LibAV.Demuxer)
    ]

    {[spec: spec], %{decoder: opts[:decoder], output_path: opts[:output_path]}}
  end

  def handle_child_notification(
        {:new_stream, stream = %{codec_name: name}},
        :demuxer,
        _ctx,
        state = %{decoder: {decoder, name}}
      ) do
    spec =
      get_child(:demuxer)
      |> via_out(Pad.ref(:output, stream.stream_index))
      # |> child(:decoder, decoder)
      |> child(:sink, %Membrane.File.Sink{location: state.output_path})

    {[spec: spec], state}
  end
end
