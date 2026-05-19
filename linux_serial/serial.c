/**
 * @file      : serial.c
 * @brief     : Linux平台串口驱动源文件
 * @author    : huenrong (huenrong1028@outlook.com)
 * @date      : 2023-01-18 14:28:05
 *
 * @copyright : Copyright (c) 2023 huenrong
 *
 * @history   : date       author          description
 *              2023-02-05 huenrong        1. 删除无用变量
 *                                         2. 修改函数调用错误问题
 *              2023-01-18 huenrong        创建文件
 *
 */

#include <assert.h>
#include <fcntl.h>
#include <linux/serial.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "./serial.h"

// 串口设备名最大长度
#define SERIAL_DEV_NAME_MAX_LEN 15

// 串口设备最大数量
#define SERIAL_DEV_MAX_NUM 10

// 串口信息结构体
typedef struct
{
    char serial_dev_name[SERIAL_DEV_NAME_MAX_LEN]; // 串口设备名
    int serial_dev_fd;                             // 串口设备文件描述符
    pthread_mutex_t serial_dev_mutex;              // 串口设备互斥锁
} serial_dev_info_t;

// 已打开的串口设备数量
static uint8_t g_serial_dev_num = 0;
// 串口设备信息
static serial_dev_info_t g_serial_dev_info[SERIAL_DEV_MAX_NUM];

/**
 * @brief  查找指定串口设备信息
 * @param  serial_dev_info: 输出参数, 查找到的串口设备设备信息
 * @param  serial_dev_name: 输入参数, 待查找的串口设备名
 * @return true : 成功
 * @return false: 失败
 */
static bool serial_find_dev_info(serial_dev_info_t *serial_dev_info, const char *serial_dev_name)
{
    assert((serial_dev_info != NULL) && (serial_dev_name != NULL));

    int ret = -1;

    for (uint8_t i = 0; i < g_serial_dev_num; i++)
    {
        ret = memcmp(g_serial_dev_info[i].serial_dev_name, serial_dev_name, strlen(serial_dev_name));
        if (0 == ret)
        {
            memcpy(serial_dev_info, &g_serial_dev_info[i], sizeof(serial_dev_info_t));

            return true;
        }
    }

    return false;
}

/**
 * @brief  设置串口标准波特率
 * @param  options  : 输出参数, 串口属性
 * @param  baud_rate: 输入参数, 波特率
 * @return true : 成功
 * @return false: 失败
 */
static bool serial_set_std_baud_rate(struct termios *options, const serial_baud_rate_e baud_rate)
{
    assert(options != NULL);

    // 设置输入波特率
    if (0 != cfsetispeed(options, baud_rate))
    {
        return false;
    }

    // 设置输出波特率
    if (0 != cfsetospeed(options, baud_rate))
    {
        return false;
    }

    return true;
}

/**
 * @brief  设置串口特殊波特率
 * @param  options  : 输出参数, 串口属性
 * @param  fd       : 输入参数, 文件描述符
 * @param  baud_rate: 输入参数, 波特率
 * @return true : 成功
 * @return false: 失败
 */
static bool serial_set_special_baud_rate(struct termios *options, const int fd, const int baud_rate)
{
    assert(options != NULL);

    struct serial_struct serial = {0};

    // 设置波特率为38400
    if (0 != cfsetispeed(options, B38400))
    {
        return false;
    }

    if (0 != cfsetospeed(options, B38400))
    {
        return false;
    }

    if (0 != ioctl(fd, TIOCGSERIAL, &serial))
    {
        return false;
    }

    // 设置标志位和系数
    serial.flags = ASYNC_SPD_CUST;
    serial.custom_divisor = (serial.baud_base / baud_rate);
    if (0 != ioctl(fd, TIOCSSERIAL, &serial))
    {
        return false;
    }

    return true;
}

/**
 * @brief  设置串口数据位
 * @param  options : 输出参数, 串口属性
 * @param  data_bit: 输入参数, 数据位
 */
static void serial_set_data_bit(struct termios *options, const serial_data_bit_e data_bit)
{
    assert(options != NULL);

    options->c_cflag &= ~CSIZE;
    options->c_cflag |= data_bit;
}

/**
 * @brief  设置串口奇偶检验位
 * @param  options   : 输出参数, 串口属性
 * @param  parity_bit: 输入参数, 奇偶检验位, 默认为无校验'n'或'N'
 */
static void serial_set_parity_bit(struct termios *options, const serial_parity_bit_e parity_bit)
{
    assert(options != NULL);

    switch (parity_bit)
    {
    // 无校验
    case E_SERIAL_PARITY_BIT_N:
    {
        options->c_cflag &= ~PARENB;
        options->c_iflag &= ~INPCK;

        break;
    }

    // 奇校验
    case E_SERIAL_PARITY_BIT_O:
    {
        options->c_cflag |= (PARODD | PARENB);
        options->c_iflag |= INPCK;

        break;
    }

    // 偶校验
    case E_SERIAL_PARITY_BIT_E:
    {
        options->c_cflag |= PARENB;
        options->c_cflag &= ~PARODD;
        options->c_iflag |= INPCK;

        break;
    }

    // 默认为无校验
    default:
    {
        options->c_cflag &= ~PARENB;
        options->c_iflag &= ~INPCK;

        break;
    }
    }
}

/**
 * @brief  设置串口停止位
 * @param  options : 输出参数, 串口属性
 * @param  stop_bit: 输入参数, 停止位, 默认为1位停止位
 */
static void serial_set_stop_bit(struct termios *options, const serial_stop_bit_e stop_bit)
{
    assert(options != NULL);

    switch (stop_bit)
    {
    // 1位停止位
    case E_SERIAL_STOP_BIT_1:
    {
        options->c_cflag &= ~CSTOPB;

        break;
    }

    // 2位停止位
    case E_SERIAL_STOP_BIT_2:
    {
        options->c_cflag |= CSTOPB;

        break;
    }

    // 默认为1位停止位
    default:
    {
        options->c_cflag &= ~CSTOPB;

        break;
    }
    }
}

bool serial_open(const char *serial_dev_name, const serial_baud_rate_e std_baud_rate, const int special_baud_rate,
                 const serial_data_bit_e data_bit, const serial_parity_bit_e parity_bit,
                 const serial_stop_bit_e stop_bit)
{
    assert(serial_dev_name != NULL);

    int fd = -1;
    serial_dev_info_t serial_dev_info = {0};
    struct termios options = {0};

    if (g_serial_dev_num > SERIAL_DEV_MAX_NUM)
    {
        return false;
    }

    if (serial_find_dev_info(&serial_dev_info, serial_dev_name))
    {
        serial_close(serial_dev_info.serial_dev_name);
    }

    fd = open(serial_dev_name, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0)
    {
        return false;
    }

    if (0 != tcgetattr(fd, &options))
    {
        close(fd);
        return false;
    }

    if (E_SERIAL_BAUD_RATE_SPECIAL != std_baud_rate)
    {
        if (!serial_set_std_baud_rate(&options, std_baud_rate))
        {
            close(fd);
            return false;
        }
    }
    else
    {
        if (!serial_set_special_baud_rate(&options, fd, special_baud_rate))
        {
            close(fd);
            return false;
        }
    }

    serial_set_data_bit(&options, data_bit);
    serial_set_parity_bit(&options, parity_bit);
    serial_set_stop_bit(&options, stop_bit);

    options.c_cflag |= (CLOCAL | CREAD);
    options.c_oflag &= ~(OPOST);
    options.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    options.c_iflag &= ~(ICRNL | INLCR | IGNCR | IXON | IXOFF | IXANY);

    if (0 != tcflush(fd, TCIOFLUSH))
    {
        close(fd);
        return false;
    }

    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 0;

    if (0 != tcsetattr(fd, TCSANOW, &options))
    {
        close(fd);
        return false;
    }

    memcpy(g_serial_dev_info[g_serial_dev_num].serial_dev_name, serial_dev_name, strlen(serial_dev_name));
    g_serial_dev_info[g_serial_dev_num].serial_dev_fd = fd;
    pthread_mutex_init(&g_serial_dev_info[g_serial_dev_num].serial_dev_mutex, NULL);
    g_serial_dev_num++;

    return true;
}

bool serial_close(const char *serial_dev_name)
{
    assert(serial_dev_name != NULL);

    int ret = -1;

    for (uint8_t i = 0; i < g_serial_dev_num; i++)
    {
        ret = memcmp(g_serial_dev_info[i].serial_dev_name, serial_dev_name, strlen(serial_dev_name));
        if (0 == ret)
        {
            pthread_mutex_lock(&g_serial_dev_info[i].serial_dev_mutex);

            ret = close(g_serial_dev_info[i].serial_dev_fd);
            if (ret < 0)
            {
                pthread_mutex_unlock(&g_serial_dev_info[i].serial_dev_mutex);
                return false;
            }

            pthread_mutex_unlock(&g_serial_dev_info[i].serial_dev_mutex);
            g_serial_dev_info[i].serial_dev_fd = -1;
            memset(g_serial_dev_info[i].serial_dev_name, 0, SERIAL_DEV_NAME_MAX_LEN);
            pthread_mutex_destroy(&g_serial_dev_info[i].serial_dev_mutex);

            memcpy(&g_serial_dev_info[i], &g_serial_dev_info[i + 1],
                   (sizeof(serial_dev_info_t) * (SERIAL_DEV_MAX_NUM - i - 1)));

            if (g_serial_dev_num > 0)
            {
                g_serial_dev_num--;
            }

            return true;
        }
    }

    return true;
}

bool serial_flush_input_cache(const char *serial_dev_name)
{
    assert(serial_dev_name != NULL);

    serial_dev_info_t serial_dev_info = {0};

    if (!serial_find_dev_info(&serial_dev_info, serial_dev_name))
    {
        return true;
    }

    pthread_mutex_lock(&serial_dev_info.serial_dev_mutex);

    if (0 == tcflush(serial_dev_info.serial_dev_fd, TCIFLUSH))
    {
        pthread_mutex_unlock(&serial_dev_info.serial_dev_mutex);
        return true;
    }

    pthread_mutex_unlock(&serial_dev_info.serial_dev_mutex);
    return false;
}

bool serial_flush_output_cache(const char *serial_dev_name)
{
    assert(serial_dev_name != NULL);

    serial_dev_info_t serial_dev_info = {0};

    if (!serial_find_dev_info(&serial_dev_info, serial_dev_name))
    {
        return true;
    }

    pthread_mutex_lock(&serial_dev_info.serial_dev_mutex);

    if (0 == tcflush(serial_dev_info.serial_dev_fd, TCOFLUSH))
    {
        pthread_mutex_unlock(&serial_dev_info.serial_dev_mutex);
        return true;
    }

    pthread_mutex_unlock(&serial_dev_info.serial_dev_mutex);
    return false;
}

bool serial_flush_both_cache(const char *serial_dev_name)
{
    assert(serial_dev_name != NULL);

    serial_dev_info_t serial_dev_info = {0};

    if (!serial_find_dev_info(&serial_dev_info, serial_dev_name))
    {
        return true;
    }

    pthread_mutex_lock(&serial_dev_info.serial_dev_mutex);

    if (0 == tcflush(serial_dev_info.serial_dev_fd, TCIOFLUSH))
    {
        pthread_mutex_unlock(&serial_dev_info.serial_dev_mutex);
        return true;
    }

    pthread_mutex_unlock(&serial_dev_info.serial_dev_mutex);
    return false;
}

int serial_write_data(const char *serial_dev_name, const uint8_t *send_data, const uint32_t send_data_len)
{
    assert((serial_dev_name != NULL) && (send_data != NULL) && (send_data_len > 0));

    int ret = -1;
    serial_dev_info_t serial_dev_info = {0};

    if (!serial_find_dev_info(&serial_dev_info, serial_dev_name))
    {
        return -1;
    }

    pthread_mutex_lock(&serial_dev_info.serial_dev_mutex);
    ret = write(serial_dev_info.serial_dev_fd, send_data, send_data_len);
    pthread_mutex_unlock(&serial_dev_info.serial_dev_mutex);

    return ret;
}

int serial_read_data(uint8_t *recv_data, const char *serial_dev_name, const size_t recv_data_len, uint32_t timeout)
{
    assert((recv_data != NULL) && (serial_dev_name != NULL) && (recv_data_len > 0));

    int ret = -1;
    nfds_t nfds = 1;
    struct pollfd fds[1] = {0};
    size_t total_data_len = 0;
    size_t remain_data_len = 0;
    serial_dev_info_t serial_dev_info = {0};

    if (!serial_find_dev_info(&serial_dev_info, serial_dev_name))
    {
        return -1;
    }

    memset(recv_data, 0, recv_data_len);
    remain_data_len = recv_data_len;

    pthread_mutex_lock(&serial_dev_info.serial_dev_mutex);

    while (1)
    {
        memset(fds, 0, sizeof(fds));
        fds[0].fd = serial_dev_info.serial_dev_fd;
        fds[0].events = POLLIN;

        ret = poll(fds, nfds, timeout);
        if (ret < 0)
        {
            pthread_mutex_unlock(&serial_dev_info.serial_dev_mutex);
            return -1;
        }
        else if (0 == ret)
        {
            if (total_data_len > 0)
            {
                pthread_mutex_unlock(&serial_dev_info.serial_dev_mutex);
                return (int)total_data_len;
            }

            pthread_mutex_unlock(&serial_dev_info.serial_dev_mutex);
            return -1;
        }
        else if (fds[0].revents & POLLIN)
        {
            lseek(fds[0].fd, 0, SEEK_SET);
            ret = read(fds[0].fd, &recv_data[total_data_len], remain_data_len);
            if (ret < 0)
            {
                pthread_mutex_unlock(&serial_dev_info.serial_dev_mutex);
                return -1;
            }

            total_data_len += (size_t)ret;
            remain_data_len = recv_data_len - total_data_len;
            if (total_data_len == recv_data_len)
            {
                break;
            }
        }
    }

    pthread_mutex_unlock(&serial_dev_info.serial_dev_mutex);
    return (int)total_data_len;
}
