# soph-2026

Computes the Sophomore's dream constant $\sum_{n=1}^{\infty} n^{-n}$ to arbitrary precision.

Read my blog post [here](https://max.pm/posts/soph-2026/).

## Usage

```
make
./compute.sh 1000    # 1000 digits
```

**Use GMP 6.3.0+, because prior versions have a bug in the FFT multiplication code.**

Remember to add guard digits. For $D$ working digits and $N$ terms satisfying $N^N \ge 10^D$, the combined error from per-term flooring and the omitted tail is less than $N10^{-D}$, and the approximation is always below the true value. The results in `results/` are _untruncated_, so their final digits are not guaranteed to be correct.

To certify the retained digits after discarding $g$ guard digits, let $A$ be the integer output of `soph` and check $\lfloor A/10^g \rfloor = \lfloor (A+N)/10^g \rfloor$. This ensures the error cannot carry into the retained digits. The saved million- and two-million-digit results pass this check with 100 guard digits.

## Algorithm

Terms are distributed across workers and summed via a balanced in-place reduction tree:

```
                         p[0]                    <- result
                          |
              stride=4    |
                +---------+---------+
               p[0]                p[4]
                |                   |
    stride=2    |                   |
              +-+--+            +--+--+
            p[0]  p[2]        p[4]  p[6]
              |     |           |     |
  stride=1    |     |           |     |
            +-+-+ +-+--+     +--+-+ +--+-+
           p[0]p[1]p[2]p[3] p[4]p[5]p[6]p[7]     <- workers
```

## License

0BSD. See [LICENSE](LICENSE).
