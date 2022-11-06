# Simple CH347 SPI-NOR programmer

SPI-NOR programmer software for WCH CH347 USB 2.0 High-speed USB to UART+SPI+I2C bridge using libusb.

Based on [ch341prog](https://github.com/hackpascal/ch341prog) by Hackpascal.

Tested under linux. It should work under Windows with winusb driver installed but I haven't tried it yet.

## Performance

The maximum SPI frequency supported by this chip is 60MHz and it can achieve a read speed of about 4950KB/s.

Pretty impressive for a 32CNY board :)