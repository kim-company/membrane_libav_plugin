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

  def streams(_ctx) do
    raise "NIF streams/1 not implemented"
  end

  def is_ready(_ctx) do
    raise "NIF is_ready/1 not implemented"
  end

  def demand(_ctx) do
    raise "NIF demand/1 not implemented"
  end
end
