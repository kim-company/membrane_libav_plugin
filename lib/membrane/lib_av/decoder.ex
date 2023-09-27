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
    accepted_format: Membrane.RawAudio,
    flow_control: :auto
  )

  @impl true
  def handle_init(_ctx, opts) do
    if opts.stream.codec_type != :audio do
      raise "Unsupported codec_type != :audio"
    end

    {[],
     %{
       stream: opts.stream,
       ctx: LibAV.decoder_alloc_context(opts.stream.codec_id, opts.stream.codec_params)
     }}
  end

  @impl true
  def handle_stream_format(:input, _format, _ctx, state) do
    stream_format = LibAV.decoder_stream_format(state.ctx)

    {[
       # {:output, %Membrane.RemoteStream{content_format: to_raw_audio_format(stream_format)}}
       stream_format: {:output, to_raw_audio_format(stream_format)}
     ], state}
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
        buffers =
          Enum.map(frames, fn frame ->
            %Membrane.Buffer{
              payload: frame.data,
              pts: frame.pts
            }
          end)

        {key, buffers}

      {:error, error} ->
        raise to_string(error)
    end
  end

  defp to_raw_audio_format(stream_format) do
    {sample_type, sample_size} =
      case to_string(stream_format.sample_format) do
        "u8" -> {:u, 8}
        "s16" -> {:s, 16}
        "s32" -> {:s, 32}
        "flt" -> {:f, 32}
        "dbl" -> {:f, 64}
        other -> raise "Sample format #{inspect(other)} not supported"
      end

    endianness =
      case System.endianness() do
        :little -> :le
        :big -> :be
      end

    %Membrane.RawAudio{
      sample_rate: stream_format.sample_rate,
      channels: stream_format.channels,
      sample_format:
        Membrane.RawAudio.SampleFormat.from_tuple({sample_type, sample_size, endianness})
    }
  end
end
