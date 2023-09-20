defmodule Membrane.LibAV.Demuxer do
  use Membrane.Filter
  alias Membrane.LibAV.Demuxer.Nif

  # TODO
  # might be an idea to ask for bytes instead of buffers.
  def_input_pad(:input,
    availability: :always,
    accepted_format: Membrane.RemoteStream,
    flow_control: :manual
  )

  def_output_pad(:output,
    availability: :on_request,
    accepted_format: Membrane.RemoteStream,
    flow_control: :manual
  )

  @impl true
  def handle_init(_ctx, _opts) do
    {[],
     %{
       ctx: Nif.alloc_context()
     }}
  end

  @impl true
  def handle_playing(_ctx, state) do
    # Start by asking some buffers, which are going to be used
    # to discover the available streams.
    {[demand: {:input, 1}], state}
  end

  @impl true
  def handle_stream_format(:input, _format, _ctx, state) do
    # We cannot have any output pad at this point, as those are
    # linked based on the information we find in the input stream.
    {[], state}
  end

  @impl true
  def handle_buffer(:input, buffer, _ctx, state) do
    IO.inspect([pts: buffer.pts, size: byte_size(buffer.payload)], label: "BUFFER RECEIVED")
    :ok = Nif.add_data(state.ctx, buffer.payload)
    :ok = Nif.detect_streams(state.ctx)
    {[], state}
  end
end
