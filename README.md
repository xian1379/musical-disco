# linux_serial_demo


这个项目主要演示一条最基本的串口测试流程：

1. 打开串口
2. 发送一帧固定测试数据
3. 清空串口缓存
4. 接收返回数据
5. 关闭串口

## Build

```bash
make
```

编译成功后会在 `output/` 目录下生成可执行文件：

```bash
./output/linux_serial_demo
```

## Usage

程序现在不再写死串口名，运行时通过命令行传入，支持 1 个或 2 个串口参数：

```bash
./output/linux_serial_demo <serial0>
./output/linux_serial_demo <serial0> <serial1>
```

示例：

```bash
./output/linux_serial_demo /dev/ttyAMA0
./output/linux_serial_demo /dev/ttyS0 /dev/ttyUSB0
```

## Default Serial Settings

当前程序固定使用以下串口参数：

- 波特率：`115200`
- 数据位：`8`
- 校验位：`N`
- 停止位：`1`

也就是常见的 `115200 8N1`。

## Test Data

发送的固定测试数据为：

```text
AA 01 03 11 22 33 A8
```

程序会把发送和接收的数据都按十六进制打印出来。

## Run Notes

- 这是一个 Linux 环境下的串口测试程序，不依赖特定开发板型号。
- 但运行环境必须存在可访问的串口设备节点，例如 `/dev/ttyS0`、`/dev/ttyUSB0`、`/dev/ttyAMA0`。
- 当前 Linux 用户需要有串口访问权限；常见做法是加入 `dialout` 组。
- 如果串口能打开但没有回环线、没有对端设备、或者没有返回数据，程序会出现接收失败。

## Test Steps

下面是最常见的测试方式。

### 1. 确认串口设备名

先在 Linux 里查看有哪些串口设备：

```bash
ls /dev/ttyS* /dev/ttyUSB* /dev/ttyAMA* /dev/ttyACM* 2>/dev/null
```

从输出里选一个实际存在的设备名，例如：

```bash
/dev/ttyS0
/dev/ttyUSB0
```

### 2. 确认当前用户有串口权限

查看当前用户所在的组：

```bash
id
```

如果输出里没有 `dialout`，可以执行：

```bash
sudo usermod -aG dialout $USER
```

然后重新登录终端或重新登录系统后再测试。

### 3. 连接测试硬件

有两种常见测试方式：

- 回环测试：把同一个串口的 `TX` 和 `RX` 短接
- 对接测试：把串口接到一个会返回数据的外设

如果没有回环线或对端设备响应，程序通常会发送成功但接收失败。

### 4. 运行程序

单串口测试：

```bash
./output/linux_serial_demo /dev/ttyS0
```

双串口测试：

```bash
./output/linux_serial_demo /dev/ttyS0 /dev/ttyUSB0
```

## Output Guide

下面列出常见运行结果，方便判断当前状态。

### 1. 参数错误

如果没有传串口参数，或者传了超过两个参数：

```text
usage: ./output/linux_serial_demo <serial0> [serial1]
```

这表示程序本身正常启动了，但命令行参数不符合要求。

### 2. 串口打开失败

如果串口不存在、没有权限、设备被占用、或者设备节点虽然存在但底层不可用，会看到：

```text
open /dev/ttyS0 fail
```

如果第二个串口打开失败，还会多一条关闭第一个串口的输出：

```text
open /dev/ttyS1 fail
close serial /dev/ttyS0
```

这表示程序没进入收发阶段，问题出在串口打开阶段。

### 3. 串口发送成功，但接收失败

如果串口可以打开，也能发数据，但没有收到返回数据，会看到：

```text
serial /dev/ttyS0 write data[len = 7]: 0xAA 0x01 0x03 0x11 0x22 0x33 0xA8
serial /dev/ttyS0 recv data fail
close serial /dev/ttyS0
```

这通常说明：

- 串口权限已经没问题
- 串口设备已经打开成功
- 但没有回环，或者对端没有回数据

### 4. 串口发送失败

如果串口已经打开，但写数据失败，会看到：

```text
serial /dev/ttyS0 write data[len = 7]: 0xAA 0x01 0x03 0x11 0x22 0x33 0xA8
serial /dev/ttyS0 send data fail
```

这表示问题出在发送阶段。

### 5. 清空串口缓存失败

如果串口缓存清理失败，会看到：

```text
serial /dev/ttyS0 flush both data fail
```

这表示串口已打开，但缓存操作失败。

### 6. 串口收发成功

如果串口接线和回传都正常，预期输出如下：

```text
serial /dev/ttyS0 write data[len = 7]: 0xAA 0x01 0x03 0x11 0x22 0x33 0xA8
serial /dev/ttyS0 read data[len = 7]: 0xAA 0x01 0x03 0x11 0x22 0x33 0xA8
close serial /dev/ttyS0
```

这表示：

- 串口打开成功
- 数据发送成功
- 数据接收成功
- 整个测试链路正常

### 7. 双串口测试时的分隔输出

如果传入两个串口，两个测试之间会打印分隔符：

```text
******************************************************
```

示例：

```text
serial /dev/ttyS0 write data[len = 7]: 0xAA 0x01 0x03 0x11 0x22 0x33 0xA8
serial /dev/ttyS0 read data[len = 7]: 0xAA 0x01 0x03 0x11 0x22 0x33 0xA8

******************************************************
serial /dev/ttyUSB0 write data[len = 7]: 0xAA 0x01 0x03 0x11 0x22 0x33 0xA8
serial /dev/ttyUSB0 read data[len = 7]: 0xAA 0x01 0x03 0x11 0x22 0x33 0xA8

******************************************************
close serial /dev/ttyS0
close serial /dev/ttyUSB0
```

## How To Judge The Result

可以按下面方式快速判断：

- 看到 `usage:`：命令行参数不对
- 看到 `open ... fail`：串口连接失败，程序未开始收发
- 看到 `write data...` 但后面是 `recv data fail`：串口打开成功，但链路没有返回数据
- 看到 `read data[len = ...]`：串口连接和收发都成功
- 看到 `close serial ...`：程序已经走到关闭串口阶段

## Verified Environment

已在 Ubuntu 24.04 虚拟机中完成以下验证：

- `make` 编译成功
- 程序可以按新的命令行参数方式启动
- 串口设备权限问题已定位为 Linux 用户权限问题，而不是程序参数问题

需要注意：

- 若串口没有接回环或外设响应，程序会在发送后读取失败
- 若某个设备节点存在但底层不可用，打开串口也可能失败

## Dependency

项目依赖本仓库内的 `linux_serial/` 目录：

- `linux_serial/serial.h`
- `linux_serial/serial.c`

如果这两个文件缺失，项目将无法编译。
