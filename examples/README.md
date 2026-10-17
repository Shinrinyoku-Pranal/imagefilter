# Example images

`input.png` is a generated 960×540 test pattern. The other images show Gaussian blur (radius 5, sigma 2), Sobel edges, resize to 480×270, and the full blur → edge → resize pipeline.

You can generate a larger input with:

```sh
mkdir -p results
./build/imagefilter generate --output results/input-4k.png --size 3840x2160
```

The synthetic pattern is included so the example images do not depend on an external photo or download.
