defmodule Membrane.LibAV.Demuxer.Filter do
  use Membrane.Filter
  alias Membrane.LibAV.Demuxer

  require Membrane.Logger

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
       ctx: Demuxer.alloc_context(),
       format_detected?: false,
       available_streams: [],
       streams: %{}
     }}
  end

  @impl true
  def handle_playing(_ctx, state) do
    # Start by asking some buffers, which are going to be used
    # to discover the available streams.
    {[demand: {:input, Demuxer.demand(state.ctx)}], state}
  end

  @impl true
  def handle_end_of_stream(:input, _ctx, state = %{format_detected?: false}) do
    # We cannot wait for the demuxer to become ready, we need to
    # try with what we've collected.
    :ok = Demuxer.add_data(state.ctx, nil)
    publish_streams(state)
  end

  def handle_end_of_stream(:input, ctx, state) do
    # EOS is controlled by the internal demuxer.
    :ok = Demuxer.add_data(state.ctx, nil)
    demux_buffers(ctx, state)
  end

  @impl true
  def handle_stream_format(:input, _format, _ctx, state) do
    # We cannot have any output pad at this point, as those are
    # linked based on the information we find in the input stream.
    {[], state}
  end

  @impl true
  def handle_pad_added(pad = {Membrane.Pad, :output, _stream_index}, _ctx, state) do
    streams = Map.put_new(state.streams, pad, [])
    {[stream_format: {pad, %Membrane.RemoteStream{}}], %{state | streams: streams}}
  end

  # NOTE
  # We expect each output pad to be attached before demand comes in.
  # The stream_index filtering process makes the filter throw away buffers
  # that comes from an untracked stream_index.
  #
  # Things go wrong if we attach two outputs, the demand for the first comes
  # before the second is attached and we try to fullfill the demand. There, we
  # probably throw away data that is needed for the second output.

  @impl true
  def handle_demand({Membrane.Pad, :output, _stream_index}, _size, :buffers, ctx, state) do
    demux_buffers(ctx, state)
  end

  @impl true
  def handle_buffer(:input, buffer, _ctx, state = %{format_detected?: false}) do
    :ok = Demuxer.add_data(state.ctx, buffer.payload)

    if Demuxer.is_ready(state.ctx) do
      publish_streams(state)
    else
      {[demand: {:input, Demuxer.demand(state.ctx)}], state}
    end
  end

  def handle_buffer(:input, buffer, ctx, state) do
    :ok = Demuxer.add_data(state.ctx, buffer.payload)
    demux_buffers(ctx, state)
  end

  defp demux_buffers(ctx, state) do
    {actions, state} =
      case read_packets(state, []) do
        {:eof, packets} ->
          {[], load_packets(ctx, state, packets)}

        {:demand, demand, packets} ->
          {[demand: {:input, demand}], load_packets(ctx, state, packets)}

        {:error, error} ->
          raise error
      end

    {buffer_actions, state} = dispatch_buffers(ctx, state)
    actions = actions ++ buffer_actions

    {actions, state}
  end

  defp dispatch_buffers(ctx, state) do
    ctx
    |> output_pads()
    |> Enum.flat_map_reduce(state, fn pad, state ->
      demand = get_in(ctx, [:pads, pad, :demand])

      {buffers, state} =
        get_and_update_in(state, [:streams, pad], fn buffers ->
          Enum.split(buffers, demand)
        end)

      emit_eos? =
        ctx.pads.input.end_of_stream? and length(get_in(state, [:streams, pad])) == 0

      actions =
        List.flatten([
          [buffer: {pad, buffers}],
          if(emit_eos?, do: [end_of_stream: pad], else: [])
        ])

      {actions, state}
    end)
  end

  defp load_packets(ctx, state, packets) do
    pads = output_pads(ctx)
    streams = Enum.map(pads, fn {_, _, index} -> index end)

    Enum.reduce(packets, state, fn packet, state ->
      if packet.stream_index not in streams do
        Membrane.Logger.warning(
          "Dropping packet of untracked stream_index #{inspect(packet.stream_index)}"
        )

        state
      else
        buffer =
          %Membrane.Buffer{
            pts: packet.pts,
            dts: packet.pts,
            payload: packet.data
          }

        update_in(state, [:streams, {Membrane.Pad, :output, packet.stream_index}], fn acc ->
          acc ++ [buffer]
        end)
      end
    end)
  end

  defp read_packets(state, acc) do
    case Demuxer.read_packet(state.ctx) do
      :eof ->
        {:eof, Enum.reverse(acc)}

      {:error, other} ->
        {:error, inspect(other)}

      {:demand, demand} ->
        {:demand, demand, Enum.reverse(acc)}

      {:ok, packet} ->
        read_packets(state, [packet | acc])
    end
  end

  defp publish_streams(state) do
    case Demuxer.streams(state.ctx) do
      {:ok, streams} ->
        actions =
          Enum.map(streams, fn {codec, stream_index} ->
            {:notify_parent,
             {:new_stream, %{codec_name: to_string(codec), stream_index: stream_index}}}
          end)

        {actions, %{state | format_detected?: true, available_streams: streams}}

      {:error, reason} ->
        raise to_string(reason)
    end
  end

  defp output_pads(ctx) do
    ctx.pads
    |> Enum.flat_map(fn
      {pad = {Membrane.Pad, :output, _stream_index}, _} -> [pad]
      _ -> []
    end)
  end
end
