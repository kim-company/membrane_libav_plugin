defmodule Membrane.LibAV.Demuxer do
  use Membrane.Filter
  alias Membrane.LibAV.Demuxer.Nif

  # TODO
  # might be an idea to ask for bytes instead of buffers.
  def_input_pad(:input,
    availability: :always,
    accepted_format: Membrane.RemoteStream,
    flow_control: :manual,
    demand_unit: :bytes
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
       ctx: Nif.alloc_context(),
       format_detected?: false
     }}
  end

  @impl true
  def handle_playing(_ctx, state) do
    # Start by asking some buffers, which are going to be used
    # to discover the available streams.
    {[demand: {:input, Nif.demand(state.ctx)}], state}
  end

  @impl true
  def handle_stream_format(:input, _format, _ctx, state) do
    # We cannot have any output pad at this point, as those are
    # linked based on the information we find in the input stream.
    {[], state}
  end

  @impl true
  def handle_buffer(:input, buffer, _ctx, state = %{format_detected?: false}) do
    :ok = Nif.add_data(state.ctx, buffer.payload)

    if Nif.is_ready(state.ctx) do
      {:ok, streams} = Nif.detect_streams(state.ctx)

      actions =
        Enum.map(streams, fn {codec, stream_index} ->
          {:notify_parent,
           {:new_stream, %{codec_name: to_string(codec), stream_index: stream_index}}}
        end)

      # We're not sending any demand till a pad is connected and
      # asks for it.
      {actions, %{state | format_detected?: true}}
    else
      {[demand: {:input, Nif.demand(state.ctx)}], state}
    end
  end

  # def handle_buffer(:input, buffer, _ctx, state) do

  #   :ok = Nif.add_data(state.ctx, buffer.payload)
  #   {:ok, codecs} = Nif.detect_streams(state.ctx)
  # end
end
