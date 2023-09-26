# Membrane LibAV plugin
Experimental Membrane plugin for demuxing and decoding any audio/video content with LibAV.

## Debugging
This library works with NIFs. When things go wrong, the BEAM exits!
The idea is to run the `mix test` loop inside a debugger, in my case `lldb`.

To do so, we need to set some env variables, accomplished with `. DEBUG.fish`.
To run the tests under the debugger, `lldb -- $ERLEXEC $CMD_ARGS test`.

## Status
The demuxer recognizes any stream, the decoder decodes to raw data. I tested
only audio streams (aac, opus). Error handling is incomplete.

## Resources
- https://cocoa-research.works/2022/02/debug-erlang-nif-library/
- https://andrealeopardi.com/posts/using-c-from-elixir-with-nifs/
- https://www.erlang.org/doc/man/erl_nif
- https://lldb.llvm.org/use/map.html#breakpoint-commands

