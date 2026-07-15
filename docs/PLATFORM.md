# Shark Win32 Platform Shell

- **Increment:** `F-004` host contract; DPI policy completed by `G-002`
- **Last verified:** July 15, 2026

F-004 establishes the Windows host boundary used by later graphics and
simulation increments. `SharkSandbox` remains a console-subsystem executable so
structured stderr diagnostics stay visible while it owns one native Unicode
Win32 window.

The public platform headers contain no `windows.h` types. Win32 implementation
details remain under `engine/platform/windows`, and the D3D12 presentation
layer obtains the HWND through an explicitly opaque `NativeWindowHandle`.

## Ownership and lifecycle

`Application::create` validates a UTF-8 title and a nonzero client extent,
registers Shark's process-lifetime window class, and creates one top-level
window. The movable public object owns a heap-backed implementation so the
pointer stored in `GWLP_USERDATA` never changes when the public object moves.

The application and its native window are thread-affine:

1. create the application on the thread that will own its message queue;
2. call every state, event-span, native-handle, show, resize, minimize, restore,
   pump, and destruction operation only on that thread;
3. process the current event span, then call `clear_events`; and
4. destroy the application on its creating thread.

Wrong-thread operations that can report failure return `ErrorCategory::platform` with
`ErrorCode::invalid_state`. Wrong-thread destruction is a nonrecoverable
invariant because freeing the callback state while an HWND still references it
would be unsafe.

`request_close` is the one cross-thread posting operation. It reads an atomic
close target and posts `WM_CLOSE`; callers must still synchronize the
`Application` object's lifetime and `close_window` operation so the object
cannot move and the HWND cannot be destroyed during that call.

The window procedure clears `GWLP_USERDATA` during `WM_NCDESTROY`. No
application callback runs from inside the native procedure, and the procedure
performs no dynamic event allocation, so no C++ exception crosses the Win32
callback boundary.

## Message and idle loop

`poll_events` drains the current thread's Win32 queue without blocking,
translates and dispatches messages, and reports `WM_QUIT` plus its exit code.
When the window remains open and no work is pending, `wait_for_events` uses
`WaitMessage`. The GPU-independent F-004 smoke and a minimized interactive
presentation therefore sleep, while a usable G-002 window polls and presents
continuously.

`WM_CLOSE` produces `WindowCloseRequestedEvent` but does not immediately
destroy the window. The sandbox currently accepts that request by calling
`close_window`. This separation leaves room for future unsaved-work or editor
policy without placing policy in the Win32 callback. `WM_DESTROY` publishes
`WindowClosedEvent` and ends the one-window application lifecycle.

## Event contract

Events are plain records in a bounded 256-entry buffer:

| Record | Meaning |
|---|---|
| `WindowCloseRequestedEvent` | Title-bar close, Alt+F4, or a posted close request |
| `WindowClosedEvent` | The owned HWND completed destruction |
| `WindowResizedEvent` | A restored or maximized client-area extent changed |
| `WindowMinimizedEvent` | The window entered the minimized state; no zero render extent is published |
| `WindowRestoredEvent` | A minimized window returned with a usable client extent |
| `KeyEvent` | Windows virtual-key, scan code, repeat count/state, extended flag, press/release, and system-key flag |
| `MouseMovedEvent` | Signed client coordinates |
| `MouseButtonEvent` | Left, right, middle, or extra button press/release with client coordinates |
| `MouseWheelEvent` | Horizontal/vertical Windows wheel delta with screen coordinates converted to client coordinates |

The buffer is bounded because native callbacks must not allocate or throw. The
first 254 entries accept ordinary window and input records; two reserved slots
guarantee that one deduplicated close request and the final closed record cannot
be lost behind an input burst. `dropped_event_count` makes ordinary overflow
visible; `clear_events` resets both the current records and the drop count.
Mouse movement is not yet coalesced.

Keyboard records deliberately retain Windows virtual-key values. Text input,
IME composition, raw input, an engine-wide key enum, action mapping, held-key
state, cursor capture, and camera controls are separate future concerns.

## DPI and physical client pixels

Both executable manifests establish Per-Monitor DPI Awareness v2 before any
window is created. Under that policy, `WindowExtent`, `WindowResizedEvent`, and
`WindowRestoredEvent` dimensions are physical client pixels and can drive DXGI
without a logical-pixel conversion.

Initial and requested client sizes use `AdjustWindowRectExForDpi` with the
system or current-window DPI. The platform then reads `GetClientRect` rather
than assuming Windows produced the requested dimensions. `WM_DPICHANGED`
applies Windows' suggested top-level rectangle; its resulting `WM_SIZE` event
publishes the next usable physical client extent. The callback never performs
DXGI work.

## Failure behavior

Invalid configuration, UTF-8 conversion failures, window-class or HWND
creation failures, message waiting failures, and close failures return owned
`Result` errors in the platform category. Win32 failures include the numeric
native error and its system message. The composition root logs a terminal
failure once and exits nonzero.

The normal command opens the interactive window until the user closes it:

```powershell
.\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe
```

Debug builds log keyboard and mouse records at Debug severity. Window lifecycle
records remain visible at Info severity in both configurations.

For manual acceptance, drag an edge and confirm resize records, minimize and
restore the window, press and release a key, move/click/scroll the mouse, then
close by both Alt+F4 and the title-bar button in separate runs. Each run must
stay responsive while idle and exit without an orphaned window.

The noninteractive smoke mode shows a real top-level window without activating
it, performs a native client resize, minimize, and restore, then waits idle. A
worker posts the thread-safe close request as the loop enters its idle wait,
waking the real message loop. The smoke succeeds only after it observes the
resize, minimize/restore, close-request, and destruction records:

```powershell
.\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe --platform-smoke
```

CTest runs that mode with a ten-second timeout in addition to deterministic
hidden-window tests for configuration errors, message translation, ordering,
buffer bounds, native lifetime, `WM_QUIT`, PMv2 awareness, and exact physical
client sizing.

## Explicit non-goals

The platform layer owns no DXGI, Direct3D 12, WARP, swap-chain, or renderer
objects. G-002 consumes only its opaque native handle and physical client
extent. Camera controls, gameplay input mapping, a fixed simulation clock,
multi-window support, fullscreen policy, and editor behavior remain deferred.
