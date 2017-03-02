# coremon &mdash; simple bar graph CPU core load visualization

Here is what coremon looks like:

![coremon screen capture](animation.gif)

Each bar shows what fraction of each core utilization is allocated to user computation (light blue) and system tasks (dark blue).  The animation above shows CPU utilization for an N-Body simulation running on my laptop, which has a dual core hyperthreaded CPU (hence four virtual CPUs.)

Coremon works by reading `/proc/cpuinfo` and `/proc/stat` on Linux.

Coremon requires [libui](https://github.com/andlabs/libui), which you will most likely need to build from source.  Libui requires gtk3.  On Ubuntu and Debian, you will need `libgtk-3-dev` installed (and of course the usual development tools such as `gcc`.)

To compile coremon, edit the `Makefile` to correctly reflect the location where `libui` is installed, and then run the commands

    make depend
    make

The executable will be called `coremon`.

## License

Coremon is distributed under the [MIT License](https://opensource.org/licenses/MIT).

## Contact

Send comments to [david.hovemeyer@gmail.com](mailto:david.hovemeyer@gmail.com).
