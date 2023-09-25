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
       ctx_eof: false,
       format_detected?: false,
       available_streams: [],
       streams: %{}
     }}
  end

  defp demand(ctx) do
    Demuxer.demand(ctx)
  end

  @impl true
  def handle_playing(_ctx, state) do
    # Start by asking some buffers, which are going to be used
    # to discover the available streams.
    {[demand: {:input, demand(state.ctx)}], state}
  end

  @impl true
  def handle_end_of_stream(:input, _ctx, state = %{format_detected?: false}) do
    # We cannot wait for the demuxer to become ready, we need to
    # try with what we've collected.
    publish_streams(state)
  end

  def handle_end_of_stream(:input, _ctx, state) do
    # EOS is controlled by the internal demuxer.
    {[], state}
  end

  @impl true
  def handle_stream_format(:input, _format, _ctx, state) do
    # We cannot have any output pad at this point, as those are
    # linked based on the information we find in the input stream.
    {[], state}
  end

  @impl true
  def handle_pad_added(pad = {Membrane.Pad, :output, stream_index}, _ctx, state) do
    streams = Map.put_new(state.streams, stream_index, [])
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

  # TODO
  # * we need a function that loads buffers inside the state as long as the
  #   demuxer is not requesting any data
  # * function that fulfills the demand. It is also responsible for asking for demand
  #   and publishing the end_of_stream message, as it knows which pads are complete
  # * function that asks for demand
  # * function that releases all stored data in one shot (for end_of_stream)

  @impl true
  def handle_demand(pad = {Membrane.Pad, :output, stream_index}, _size, :buffers, ctx, state) do
    state = flush_demuxer(ctx, state)
    buffers = get_in(state, [:streams, stream_index])

    {[buffer: {pad, buffers}, end_of_stream: pad], state}

    # case should_demand_more(ctx, state) do
    #   {true, demand} ->
    #     {[demand: {:input, demand}], state}

    #   {false, _} ->
    #     state = reload_streams(ctx, state)
    #     {actions, state} = handle_fulfill_demand(ctx, state)
    #     {actions ++ [demand: {:input, demand(state.ctx)}], state}
    # end
  end

  @impl true
  def handle_buffer(:input, buffer, _ctx, state = %{format_detected?: false}) do
    Membrane.Logger.debug("Received #{inspect(byte_size(buffer.payload))} bytes")
    :ok = Demuxer.add_data(state.ctx, buffer.payload)

    if Demuxer.is_ready(state.ctx) do
      publish_streams(state)
    else
      {[demand: {:input, demand(state.ctx)}], state}
    end
  end

  def handle_buffer(:input, buffer, ctx, state) do
    :ok = Demuxer.add_data(state.ctx, buffer.payload)

    case should_demand_more(ctx, state) do
      {true, demand} ->
        {[demand: {:input, demand}], state}

      {false, _} ->
        state = reload_streams(ctx, state)
        {actions, state} = handle_fulfill_demand(ctx, state)
        {actions ++ [demand: {:input, demand(state.ctx)}], state}
    end
  end

  defp should_demand_more(ctx, state) do
    # The nif does not differentiate a real eof from its buffer being
    # temporarily empty. For this reason, fill the buffer on demand
    # first and fulfill the demand when new data is loaded.
    demand = demand(state.ctx)
    {demand > 0 and not ctx.pads.input.end_of_stream?, demand}
  end

  defp reload_streams(ctx, state) do
    tracked = tracked_stream_indexes(ctx)

    state =
      case Demuxer.read_packet(state.ctx) do
        {:error, :eof} ->
          %{state | ctx_eof: true}

        {:error, other} ->
          raise to_string(other)

        {:ok, packet} ->
          if packet.stream_index not in tracked do
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

            update_in(state, [:streams, packet.stream_index], fn acc ->
              acc ++ [buffer]
            end)
          end
      end

    cond do
      state.ctx_eof -> state
      demand(state.ctx) == 0 -> reload_streams(ctx, state)
      true -> state
    end
  end

  defp flush_demuxer(ctx, state) do
    state = reload_streams(ctx, state)
    if state.ctx_eof, do: state, else: flush_demuxer(ctx, state)
  end

  defp handle_fulfill_demand(ctx, state) do
    handle_fulfill_demand(ctx, state, [])
  end

  defp handle_fulfill_demand(ctx, state, old_actions) do
    {actions, state} =
      ctx.pads
      |> Enum.flat_map_reduce(state, fn
        {ref = {Membrane.Pad, :output, stream_index}, pad}, state ->
          {buffers, state} =
            get_and_update_in(state, [:streams, stream_index], fn buffers ->
              Enum.split(buffers, pad.demand)
            end)

          emit_eos? =
            ctx.pads.input.end_of_stream? and length(get_in(state, [:streams, stream_index])) == 0

          actions =
            List.flatten([
              [buffer: {ref, buffers}],
              if(emit_eos?, do: [end_of_stream: ref], else: [])
            ])

          {actions, state}

        _pad, state ->
          {[], state}
      end)

    actions = old_actions ++ actions

    {actions, state}
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

  defp tracked_stream_indexes(ctx) do
    ctx.pads
    |> Enum.flat_map(fn
      {{Membrane.Pad, :output, stream_index}, _} -> [stream_index]
      _ -> []
    end)
  end
end
