#!/bin/bash
# Debug Probe Connection Test Script

echo "=========================================="
echo "Pico 2 W Debug Probe Connection Test"
echo "=========================================="
echo ""

# Check if Debug Probe is connected
echo "1. Checking Debug Probe USB connection..."
if lsusb | grep -q "2e8a:000c.*Debug Probe"; then
    echo "   ✓ Debug Probe detected on USB"
    lsusb | grep "Debug Probe"
else
    echo "   ✗ Debug Probe NOT detected!"
    echo "   → Reconnect Debug Probe USB cable"
    exit 1
fi
echo ""

# Check if Pico 2W is powered
echo "2. Checking for Pico devices..."
lsusb | grep "2e8a" | grep -v "Debug Probe" || echo "   (No other Pico devices visible via USB)"
echo ""

# Test OpenOCD at very low speed
echo "3. Testing SWD connection at 100 kHz..."
cd ~/.pico-sdk/openocd/0.12.0+dev/scripts
timeout 3 ~/.pico-sdk/openocd/0.12.0+dev/openocd \
    -f interface/cmsis-dap.cfg \
    -c "adapter speed 100" \
    -f target/rp2350.cfg \
    -c "init; exit" 2>&1 | grep -E "Error|Info.*ready|Info.*Listening"

if [ $? -eq 0 ]; then
    echo ""
    echo "   ✗ SWD connection FAILED"
    echo ""
    echo "=========================================="
    echo "TROUBLESHOOTING STEPS:"
    echo "=========================================="
    echo ""
    echo "The Debug Probe is detected but cannot communicate with the Pico 2W."
    echo "This usually means a wiring or power issue."
    echo ""
    echo "Physical connections required:"
    echo "  Debug Probe Pin 3 (GND)   → Pico 2W GND (any GND pin)"
    echo "  Debug Probe Pin 4 (GP2)   → Pico 2W Pin 24 (SWCLK)"
    echo "  Debug Probe Pin 5 (GP3)   → Pico 2W Pin 25 (SWDIO)"
    echo ""
    echo "Power:"
    echo "  • Pico 2W must be powered via USB or VSYS"
    echo "  • Check if the LED on Pico 2W is on"
    echo ""
    echo "If wiring is correct, try this:"
    echo "  1. Disconnect Debug Probe SWD wires from Pico 2W"
    echo "  2. Hold BOOTSEL button on Pico 2W"
    echo "  3. Press RESET or unplug/replug Pico 2W USB"
    echo "  4. Release BOOTSEL (Pico should mount as USB drive)"
    echo "  5. Reconnect Debug Probe SWD wires"
    echo "  6. Run this test again"
    echo ""
else
    echo "   ✓ SWD connection successful!"
    echo ""
    echo "Your Debug Probe is properly connected and ready to use!"
fi
