defmodule Membrane.LibAV.Demuxer.Nif do
  @on_load :load_nifs

  def load_nifs do
    :erlang.load_nif(~c"./c_src/demuxer", 0)
  end

  def alloc_context() do
    raise "NIF alloc_context/0 not implemented"
  end

  def add_data(_ctx, _data) do
    raise "NIF add_data/2 not implemented"
  end

  def detect_streams(_ctx) do
    raise "NIF detect_streams/1 not implemented"
  end
end
