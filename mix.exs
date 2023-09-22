defmodule Membrane.LibAV.MixProject do
  use Mix.Project

  def project do
    [
      app: :membrane_libav_plugin,
      version: "0.1.0",
      elixir: "~> 1.15",
      start_permanent: Mix.env() == :prod,
      elixirc_paths: elixirc_paths(Mix.env()),
      make_cwd: "c_src",
      compilers: [:elixir_make] ++ Mix.compilers(),
      deps: deps()
    ]
  end

  # Run "mix help compile.app" to learn about applications.
  def application do
    [
      extra_applications: [:logger]
    ]
  end

  # Run "mix help deps" to learn about dependencies.
  defp deps do
    [
      {:membrane_core, "~> 0.12.5"},
      {:membrane_file_plugin, "~> 0.15.0", only: :test},
      {:membrane_aac_fdk_plugin, "~> 0.16.0", only: :test},
      {:elixir_make, "~> 0.6", runtime: false}
    ]
  end

  defp elixirc_paths(:test), do: ["lib", "test/support"]
  defp elixirc_paths(_env), do: ["lib"]
end
