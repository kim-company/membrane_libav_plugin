defmodule Membrane.LibAV.Decoder do
  use Membrane.Filter

  alias Membrane.LibAV

  def_options(
    stream: [
      spec: map(),
      description: "Stream information as provided by the demuxer"
    ]
  )

  def_input_pad(:input,
    availability: :always,
    accepted_format: Membrane.RemoteStream,
    flow_control: :auto
  )

  def_output_pad(:output,
    availability: :always,
    accepted_format: Membrane.RemoteStream,
    flow_control: :auto
  )

  @impl true
  def handle_init(_ctx, opts) do
    {[],
     %{
       stream: opts.stream,
       ctx: LibAV.decoder_alloc_context(opts.stream.codec_id, opts.stream.codec_params)
     }}
  end

  @impl true
  def handle_end_of_stream(:input, _ctx, state) do
    # Turns the decoder into drain mode.
    {:eof, buffers} = decode(nil, state)
    {[buffer: {:output, buffers}, end_of_stream: :output], state}
  end

  @impl true
  def handle_buffer(:input, buffer, _ctx, state) do
    {:ok, buffers} = decode(buffer, state)
    {[buffer: {:output, buffers}], state}
  end

  defp decode(buffer, state) do
    packet =
      if buffer != nil do
        %{
          data: buffer.payload,
          pts: buffer.pts,
          dts: buffer.dts
        }
      else
        nil
      end

    case LibAV.decoder_add_data(state.ctx, packet) do
      {key, frames} when key in [:ok, :eof] ->
        {key, to_buffers(frames) |> IO.inspect(label: "BUFFERS")}

      {:error, error} ->
        raise to_string(error)
    end
  end

  defp to_buffers(frames) do
    Enum.map(frames, fn frame ->
      %Membrane.Buffer{
        payload: frame.data,
        pts: frame.pts
      }
    end)
  end
end
