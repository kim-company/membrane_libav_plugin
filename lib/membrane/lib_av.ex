defmodule Membrane.LibAV do
  @on_load :load_nifs

  def load_nifs do
    path = :filename.join(:code.priv_dir(:membrane_libav_plugin), ~c"libav")
    :erlang.load_nif(path, 0)
  end

  def demuxer_alloc_context(_probe_size) do
    raise "NIF demuxer_alloc_context/1 not implemented"
  end

  def demuxer_add_data(_ctx, _data) do
    raise "NIF demuxer_add_data/2 not implemented"
  end

  def demuxer_streams(_ctx) do
    raise "NIF demuxer_streams/1 not implemented"
  end

  def demuxer_is_ready(_ctx) do
    raise "NIF demuxer_is_ready/1 not implemented"
  end

  def demuxer_demand(_ctx) do
    raise "NIF demuxer_demand/1 not implemented"
  end

  def demuxer_read_packet(_ctx) do
    raise "NIF demuxer_read_packet/1 not implemented"
  end

  def decoder_alloc_context(_codec_id, _codec_params) do
    raise "NIF decoder_alloc_context/2 not implemented"
  end

  def decoder_add_data(_ctx, _packet) do
    raise "NIF decoder_add_data/2 not implemented"
  end

  def decoder_stream_format(_ctx) do
    raise "NIF decoder_stream_format/1 not implemented"
  end
end
