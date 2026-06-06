# Espresso

*Espresso: Constructing Cost-Efficient CXL JBOF via Inter-SSD Computing Resource Sharing!*

## Introduction
Espresso is a cost-efficient JBOF design that provisions only moderate computing resources per SSD at low monetary cost, while delivering demanded I/O performance through efficient inter-SSD resource sharing. This repository contains the simulator implementation of Espresso, which use [SimpleSSD](https://docs.simplessd.org/en/v2.0.12/index.html), a popular full-stack to evaluate SSDs and host while employing [Xerxes](https://github.com/ChaseLab-PKU/Xerxes) to model the features of CXL.

Please consider citing our paper at OSDI 2026 if you use Espresso. The BibTex is shown below:
```latex
TODO
```



## Setup
#### 1. Clone Espresso from github
```bash
git clone git@github.com:ChaseLab-PKU/Espresso.git
cd Espresso
```

#### 2. Build Espresso

```bash
cmake -DDEBUG_BUILD=on ..
make -j$(nproc)
```

If you wish to build Espresso without debug mode, replace ‘on’ with ‘off’ in the ‘cmake’ command line:

```bash
cmake -DDEBUG_BUILD=off ..
make -j$(nproc)
```



## Usage

```text
simplessd-standalone <Number of instance (N)> <Output directory> <Simulation configuration file of host>{<Simulation configuration file of instance i> <SimpleSSD configuration file of instance i>} * N
```

- **N**: number of SSD instances to simulate
- **Output directory**: directory for logs and Espresso statistics
- **Simulation configuration file of host**: host configuration
- **Simulation configuration file of instance i**: IGL/BIL/SIL configuration
- **SimpleSSD configuration file of instance i**: SimpleSSD layer configuration

**Note**: SimpleSSD is a single-threaded simulator. Simulating multiple SSDs can take a very long time, so please be patient. I recommend running multiple simulations in parallel with different configurations.

## Example

The `config/example/` directory provides a minimal two-SSD, one-to-one compute resource borrowing setup:

| File | Description |
|------|-------------|
| `host.cfg` | Host configuration |
| `ssd_hw.cfg` | Shared hardware config for both SSDs |
| `ssd0_sw.cfg` | **SSD 0 (borrower)**: random writes with a fixed total I/O size, `io_size = 256M`, `iodepth = 128` |
| `ssd1_sw.cfg` | **SSD 1 (lender)**: almost no I/O, `io_size = 0`, kept idle to lend resources |


### Run

```bash
mkdir -p output/example_1to1

./simplessd-standalone 2 output/example_1to1 \
  config/example/host.cfg \
  config/example/ssd0_sw.cfg config/example/ssd_hw.cfg \
  config/example/ssd1_sw.cfg config/example/ssd_hw.cfg
```

When the simulation finishes, the terminal prints per-SSD I/O statistics; Espresso logs are written to the output directory.

### Verifying Resource Borrowing and Lending

Inspect `espresso_resource*.txt` in the output directory:

```bash
grep -E 'borrow|lend' output/example_1to1/espresso_resource*.txt
```

You should see output similar to (ticks vary between runs):

```text
output/example_1to1/espresso_resource0.txt:tick 500894072: SSD 0 set borrow from SSD 1
```
