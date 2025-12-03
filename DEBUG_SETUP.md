# Debugging Pico 2 W with Pico Debug Probe

## Hardware Setup

### 1. Connect the Pico Debug Probe to your Pico 2 W:

**Debug Probe → Pico 2 W connections:**
- GND (Pin 3) → GND (Pin 3 or any GND)
- GP2/SWCLK (Pin 4) → SWCLK (Pin 24)
- GP3/SWDIO (Pin 5) → SWDIO (Pin 25)

**Optional UART for serial output:**
- GP4/TX (Pin 6) → GP1/RX (Pin 2) 
- GP5/RX (Pin 7) → GP0/TX (Pin 1)

### 2. Connect the Debug Probe to your PC via USB

### 3. Power your Pico 2 W (via USB or VSYS)

## Software Setup (Already Configured!)

Your project is already set up with:
- ✅ OpenOCD installed at `~/.pico-sdk/openocd/0.12.0+dev/`
- ✅ Cortex-Debug launch configurations in `.vscode/launch.json`
- ✅ Project built with Debug symbols

## Building for Debug

The project has been reconfigured for Debug mode. To rebuild:

```bash
cd /home/emilec/dev/github_ec/pico_rp6502_os/build
~/.pico-sdk/ninja/v1.12.1/ninja
```

Or use the VS Code task: **Compile Project**

## Starting a Debug Session

### Method 1: Using VS Code UI (Recommended)

1. **Set breakpoints** in your code by clicking in the gutter left of line numbers
2. Press **F5** or go to **Run and Debug** panel (Ctrl+Shift+D)
3. Select **"Pico Debug (Cortex-Debug)"** from the dropdown
4. Click the green play button

### Method 2: Using the Debug Panel

1. Open the **Run and Debug** panel (Ctrl+Shift+D)
2. Select **"Pico Debug (Cortex-Debug)"** configuration
3. Press F5 or click the green play button

## Available Debug Configurations

Your `.vscode/launch.json` has three configurations:

1. **Pico Debug (Cortex-Debug)** - Uses built-in OpenOCD (recommended)
2. **Pico Debug (Cortex-Debug with external OpenOCD)** - For when you run OpenOCD separately
3. **Pico Debug (C++ Debugger)** - Alternative debugger

## Debug Controls

Once debugging starts:
- **F5** - Continue/Start
- **F10** - Step Over
- **F11** - Step Into  
- **Shift+F11** - Step Out
- **Shift+F5** - Stop Debugging
- **Ctrl+Shift+F5** - Restart

## Viewing Variables and Registers

- **Variables panel** - Shows local and global variables
- **Watch panel** - Add custom expressions to monitor
- **Call Stack panel** - Shows function call hierarchy
- **Peripheral registers** - View hardware registers (SVD file is configured)

## Troubleshooting

### "Could not connect to target"
- Verify SWD connections (SWCLK, SWDIO, GND)
- Ensure Debug Probe is connected and recognized by your PC
- Check that Pico 2 W is powered
- Try resetting the Pico 2 W

### "OpenOCD exited with code 1"
```bash
# Manually test OpenOCD connection:
cd ~/.pico-sdk/openocd/0.12.0+dev/scripts
~/.pico-sdk/openocd/0.12.0+dev/openocd \
  -f interface/cmsis-dap.cfg \
  -f target/rp2350.cfg \
  -c "adapter speed 5000"
```

### Need to see serial output?
- Use **Serial Monitor** extension (already in recommendations)
- Or use external terminal: `screen /dev/ttyACM0 115200`

### Building for Release (when done debugging)
```bash
cd /home/emilec/dev/github_ec/pico_rp6502_os/build
~/.pico-sdk/cmake/v3.31.5/bin/cmake -DCMAKE_BUILD_TYPE=Release ..
~/.pico-sdk/ninja/v1.12.1/ninja
```

## Tips for Effective Debugging

1. **Set meaningful breakpoints** - Use conditional breakpoints (right-click on breakpoint)
2. **Use watchpoints** - Break when a variable changes value
3. **Inspect peripheral registers** - The SVD file lets you see hardware state
4. **Step through PIO code** - Debug state machine interactions
5. **Monitor memory** - Use Memory Viewer for DMA buffers and shared memory

## Current Build Configuration

- Build Type: **Debug** (with debug symbols)
- Target Board: **pico2_w**
- Binary Type: **copy_to_ram** (loaded via debugger)
- Debug symbols: **Enabled** ✅

Your binaries are located at:
- `/home/emilec/dev/github_ec/pico_rp6502_os/build/src/rp6502_ria_w.elf`
- `/home/emilec/dev/github_ec/pico_rp6502_os/build/src/rp6502_vga.elf`
