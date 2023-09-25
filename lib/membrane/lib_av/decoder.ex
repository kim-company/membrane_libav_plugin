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
  def handle_buffer(:input, _buffer, _ctx, state) do
    {[], state}
  end
end
