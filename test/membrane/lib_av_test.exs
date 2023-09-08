defmodule Membrane.LibAVTest do
  use ExUnit.Case
  doctest Membrane.LibAV

  test "greets the world" do
    assert Membrane.LibAV.hello() == :world
  end
end
