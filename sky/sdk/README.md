Contributing
============

[sky_sdk](https://github.com/domokit/sky_sdk) is generated from the
[mojo repository](https://github.com/domokit/mojo) using
[deploy_sdk.py](https://github.com/domokit/mojo/blob/master/sky/tools/deploy_sdk.py)
Static files (including this README.md) are located under
[sky/sdk](https://github.com/domokit/mojo/tree/master/sky/sdk).  Pull
requests and issue reports are glady accepted at the
[mojo repository](https://github.com/domokit/mojo)!

Sky
===

Sky is an experimental, high-performance UI framework for mobile apps. Sky helps
you create apps with beautiful user interfaces and high-quality interactive
design that run smoothly at 120 Hz.

Sky consists of two components:

1. *The Sky engine.* The engine is the core of the system. Written in C++, the
   engine provides the muscle of the Sky system. The engine provides
   several primitives, including a soft real-time scheduler and a hierarchical,
   retained-mode graphics system, that let you build high-quality apps.

2. *The Sky framework.* The [framework](packages/sky/lib/framework) makes it
   easy to build apps using Sky by providing familiar user interface widgets,
   such as buttons, infinite lists, and animations, on top of the engine using
   Dart. These extensible components follow a functional programming style
   inspired by [React](http://facebook.github.io/react/).

We're still iterating on Sky heavily, which means the framework and underlying
engine are both likely to change in incompatible ways several times, but if
you're interested in trying out the system, this document can help you get
started.

Examples
--------

Sky uses Dart and Sky applications are
[Dart Packages](https://www.dartlang.org/docs/tutorials/shared-pkgs/).
Application creation starts by creating a new directory and
adding a [pubspec.yaml](https://www.dartlang.org/tools/pub/pubspec.html):

 pubspec.yaml for your app:
```yaml
name: your_app_name
dependencies:
 sky: any
```

Once the pubspec is in place, create a `lib` directory (where your dart code
will go) and run `pub get` to download all necessary dependencies and create
the symlinks necessary for 'package:your_app_names/main.dart' includes to work.

Currently the Sky Engine assumes the entry point for your application is a
`main` function located inside a `main.sky` file at the root of the package.
`.sky` is an html-like format:
```
<sky>
<script>
import 'package:your_app_name/main.dart';

void main() {
  new HelloWorldApp();
}
</script>
</sky>
```

The rest of the application then goes inside the `lib` directory of the package
thus `lib/main.dart` would be:

```dart
import 'package:sky/framework/fn.dart';

class HelloWorldApp extends App {
  UINode build() {
    return new Text('Hello, world!');
  }
}
```

Execution starts in `main`, which creates the `HelloWorldApp`. The framework
then marks `HelloWorldApp` as dirty, which schedules it to build during the next
animation frame. Each animation frame, the framework calls `build` on all the
dirty components and diffs the virtual `UINode` hierarchy returned this frame
with the hierarchy returned last frame. Any differences are then applied as
mutations to the physical hierarchy retained by the engine.

Skip down to "Running a Sky application" to learn how to load and run this
example on your device.

For examples, please see the [examples directory](examples/).

Services
--------

Sky apps can access services from the host operating system using Mojo IPC. For
example, you can access the network using the `network_service.mojom` interface.
Although you can use these low-level interfaces directly, you might prefer to
access these services via libraries in the framework. For example, the
`fetch.dart` library wraps the underlying `network_service.mojom` in an
ergonomic interface:

```dart
import 'package:sky/framework/net/fetch.dart';

main() async {
  Response response = await fetch('example.txt');
  print(response.bodyAsString());
}
```

Set up your computer
--------------------

1. Install the Dart SDK:
  - https://www.dartlang.org/tools/download.html

2. Install the ``adb`` tool from the Android SDK:
  - https://developer.android.com/sdk/installing/index.html

3. Install the Sky SDK:
  - ``git clone https://github.com/domokit/sky_sdk.git``

4. Ensure that $DART_SDK is set to the path of your Dart SDK and 'adb'
   (inside 'platform-tools' in the android sdk) is in your $PATH.

Set up your device
------------------

Currently Sky requires an Android device running the Lollipop (or newer) version
of the Android operating system.

1. Enable developer mode on your device by visiting ``Settings > About phone``
   and tapping the ``Build number`` field five times.

2. Enable ``USB debugging`` in ``Settings > Developer options``.

3. Using a USB cable, plug your phone into your computer. If prompted on your
   device, authorize your computer to access your device.

Running a Sky application
-------------------------

The `sky` pub package includes a `sky_tool` script to assist in running
Sky applications inside the `SkyDemo.apk` harness.  The sky_tool script expects
to be run from the root directory of your application pub package.  To run
one of the examples in this SDK, try:

1. ``cd examples/stocks``

2. ``pub get`` to set up a copy of the sky package in the app directory.

3. ``./packages/sky/sky_tool start`` to start the dev server and upload your
   app to the device.
   (NOTE: add a ``--install`` flag to install SkyDemo.apk if not already
   installed on the device.)

4. Use ``adb logcat`` to view any errors or Dart print() output from the app.
   ``adb logcat -s chromium`` can be used to filter only adb messages from
   `SkyDemo.apk` (which for
   [legacy reasons](https://github.com/domokit/mojo/issues/129) still uses the
   android log tag 'chromium').

Measuring Performance
---------------------

Sky has support for generating trace files compatible with
[Chrome's about:tracing](https://www.chromium.org/developers/how-tos/trace-event-profiling-tool).

`packages/sky/sky_tool start_tracing` and `packages/sky/sky_tool stop_tracing`
are the commands to use.

Due to https://github.com/domokit/mojo/issues/127 tracing currently
requires root access on the device.

Debugging
---------

Dart's [Observatory](https://www.dartlang.org/tools/observatory/)
(VM Debugger & Profiler) support in Sky is
[in progress](https://codereview.chromium.org/1107803002) and should
be released shortly after Dart Summit 2015.

Building a standalone MyApp
---------------------------

Although it is possible to bundle the Sky Engine in your own app (instead of
running your code inside SkyDemo.apk), right now doing so is difficult.

There is one example of doing so if you're feeling brave:
https://github.com/domokit/mojo/tree/master/sky/apk/stocks

Eventually we plan to make this much easier and support platforms other than
Android, but that work is yet in progress.

Adding Services to MyApp
------------------------

[Mojo IPC](https://github.com/domokit/mojo) is an inter-process-communication
system designed to provide cross-thread, cross-process, and language-agnostic
communication between applications.  Sky uses Mojo IPC to make it possible
to write UI code in Dart and yet depend on networking code, etc. written in
another language.  Services are replicable, meaning that Dart code
written to use the `network_service` remains portable to any platform
(iOS, Android, etc.) by simply providing a 'natively' written `network_service`.

Embedders of the Sky Engine and consumers of the Sky Framework can use this
same mechanism to expose not only existing services like the
[Keyboard](https://github.com/domokit/mojo/blob/master/mojo/services/keyboard/public/interfaces/keyboard.mojom)
service to allow Sky Framework Dart code to interface with the underlying
platform's Keyboard, but also to expose any additional non-Dart business logic
to Sky/Dart UI code.

As and example, [SkyApplication](https://github.com/domokit/mojo/blob/master/sky/shell/org/domokit/sky/shell/SkyApplication.java)
exposes a mojo `network_service` (required by Sky Engine C++ code)
[SkyDemoApplication](https://github.com/domokit/mojo/blob/master/sky/apk/demo/org/domokit/sky/demo/SkyDemoApplication.java)
additionally exposes `keyboard_service` and `sensor_service` for use by the Sky
Framework from Dart.
