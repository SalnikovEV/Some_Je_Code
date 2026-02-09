#include <opencv2/opencv.hpp>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <thread>
#include <vector>
#include <iostream>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>

#pragma comment(lib, "ws2_32.lib")

// =================== ГЛОБАЛЬНЫЕ ДАННЫЕ ===================

// флаг работы программы
std::atomic<bool> running(true);

// Изображения
cv::Mat frame_0;
cv::Mat frame_dop1;
cv::Mat frame_dop2;

// Мьютекс для защиты доступа к кадру
std::mutex m_raw;
std::mutex m_dop1;
std::mutex m_dop2;

// Для синхронизации потоков, чтобы поток ожидал появления нового кадра
std::condition_variable cv_raw;
std::condition_variable cv_dop1;
std::condition_variable cv_dop2;

// Окно/Область захвата
std::string name_window = "iTunes";
int x = 0;
int y = 0;
int h = 1028;
int w = 720;

// Ресайз изображения под размеры
int h_mfi = 768;
int w_mfi = 1024;

// Передача по TCP/IP
std::string ip = "127.0.0.1";
int port1 = 5015;
int port2 = 5025;
int port3 = 5035;



// ========= ЗАХВАТ ОБЛАСТИ ЭКРАНА =========
cv::Mat capture_screen(int x, int y, int w, int h) {
    static HDC hScreen = GetDC(nullptr);
    static HDC hDC = CreateCompatibleDC(hScreen);
    static HBITMAP hBitmap = nullptr;
    static cv::Mat buffer;
    static int last_w = 0, last_h = 0;

    if (!hBitmap || w != last_w || h != last_h) {
        if (hBitmap) DeleteObject(hBitmap);
        hBitmap = CreateCompatibleBitmap(hScreen, w, h);
        SelectObject(hDC, hBitmap);
        buffer.create(h, w, CV_8UC4);
        last_w = w;
        last_h = h;
    }

    BitBlt(hDC, 0, 0, w, h, hScreen, x, y, SRCCOPY | CAPTUREBLT);

    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(bi);
    bi.biWidth = w;
    bi.biHeight = -h;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    GetDIBits(hDC, hBitmap, 0, h, buffer.data,
        reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);

    cv::Mat result;
    cv::cvtColor(buffer, result, cv::COLOR_BGRA2BGR);
    return result;
}

cv::Mat capture_window(const std::string& window_name) {
    HWND hwnd = FindWindowA(nullptr, window_name.c_str());
    if (!hwnd) {
        std::cerr << "[ERROR] Window \"" << window_name << "\" not found!" << std::endl;
        return {};
    }

    // Берём клиентскую область (без рамок)
    RECT rc;
    GetClientRect(hwnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    HDC hWindowDC = GetDC(hwnd);
    HDC hMemDC = CreateCompatibleDC(hWindowDC);

    HBITMAP hBitmap = CreateCompatibleBitmap(hWindowDC, width, height);
    HBITMAP hOld = (HBITMAP)SelectObject(hMemDC, hBitmap);

    // Пытаемся корректно снять изображение окна
    BOOL ok = PrintWindow(hwnd, hMemDC, PW_CLIENTONLY);
    if (!ok) {
        // fallback если PrintWindow не сработал
        BitBlt(hMemDC, 0, 0, width, height, hWindowDC, 0, 0, SRCCOPY);
    }

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;          // BGRA
    bi.bmiHeader.biCompression = BI_RGB;

    cv::Mat img(height, width, CV_8UC4);
    GetDIBits(hMemDC, hBitmap, 0, height, img.data,
        &bi, DIB_RGB_COLORS);

    // cleanup
    SelectObject(hMemDC, hOld);
    DeleteObject(hBitmap);
    DeleteDC(hMemDC);
    ReleaseDC(hwnd, hWindowDC);

    // OpenCV любит BGR
    cv::cvtColor(img, img, cv::COLOR_BGRA2BGR);
    return img;
}



// =================== Потоки захвата изображений ===================
void CaptureThread() {
    while (running) {
        //cv::Mat frame = capture_screen(x, y, w, h); // Область
        cv::Mat frame = capture_window(name_window); // Окно      оставить толко одно из!!!
        if (frame.empty()) continue;

        {
            std::lock_guard<std::mutex> lk(m_raw);
            frame_0 = frame;
        }
        cv_raw.notify_all();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void ModifikatorThread1() { // Ч/Б
    while (running) {
        std::unique_lock<std::mutex> lk(m_raw);
        cv_raw.wait(lk, [] { return !frame_0.empty() || !running; });
        if (!running) break;

        cv::Mat src = frame_0.clone();
        lk.unlock();

        cv::Mat gray;
        cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
        cv::cvtColor(gray, gray, cv::COLOR_GRAY2BGR);

        {
            std::lock_guard<std::mutex> g(m_dop1);
            frame_dop1 = gray;
        }
        cv_dop1.notify_all();
    }
}

void ModifikatorThread2() { // Контраст
    while (running) {
        std::unique_lock<std::mutex> lk(m_raw);
        cv_raw.wait(lk, [] { return !frame_0.empty() || !running; });
        if (!running) break;

        cv::Mat src = frame_0.clone();
        lk.unlock();

        cv::Mat out;
        src.convertTo(out, -1, 1.8, 20);

        {
            std::lock_guard<std::mutex> g(m_dop2);
            frame_dop2 = out;
        }
        cv_dop2.notify_all();
    }
}


// =================== ОТОБРАЖЕНИЕ ===================
void DisplayThread1() {
    while (running) {
        std::unique_lock<std::mutex> lk(m_raw);
        cv_raw.wait(lk, [] { return !frame_0.empty() || !running; });
        if (!running) break;

        cv::imshow("RAW", frame_0);
        if (cv::waitKey(1) == 27) running = false;
    }
}

void DisplayThread2() {
    while (running) {
        std::unique_lock<std::mutex> lk(m_dop1);
        cv_dop1.wait(lk, [] { return !frame_dop1.empty() || !running; });
        if (!running) break;

        cv::imshow("GRAY", frame_dop1);
        cv::waitKey(1);
    }
}

void DisplayThread3() {
    while (running) {
        std::unique_lock<std::mutex> lk(m_dop2);
        cv_dop2.wait(lk, [] { return !frame_dop2.empty() || !running; });
        if (!running) break;

        cv::imshow("CONTRAST", frame_dop2);
        cv::waitKey(1);
    }
}



// =================== TCP ОТПРАВКА ===================
// Функция отправки одного кадра по TCP
bool send_frame(SOCKET sock, const cv::Mat& frame) {
    // Проверка: если кадр пустой — отправлять нечего
    if (frame.empty()) return false;

    // Буфер для закодированного изображения
    std::vector<uchar> buf;

    // Кодирование изображения в JPEG для уменьшения размера
    if (!cv::imencode(".jpg", frame, buf)) return false;

    // Размер JPEG-буфера (переводим в сетевой порядок байт)
    uint32_t size = htonl(static_cast<uint32_t>(buf.size()));

    // Отправляем сначала размер изображения (4 байта)
    if (send(sock, (char*)&size, 4, 0) != 4) return false;

    // Сколько байт уже отправлено
    size_t sent = 0;

    // Отправляем JPEG-данные целиком, учитывая что send может отправить не всё сразу
    while (sent < buf.size()) {
        int n = send(
            sock,
            (char*)buf.data() + sent, // смещение в буфере
            buf.size() - sent,        // сколько осталось отправить
            0
        );

        // Ошибка или разрыв соединения
        if (n <= 0) return false;

        // Увеличиваем счётчик отправленных байт
        sent += n;
    }

    // Кадр успешно отправлен
    return true;
}

// =================== TCP ПОТОК ===================
// Поток, который ожидает кадры и отправляет их по TCP
void TcpThread(
    int port,                     // порт для подключения
    cv::Mat* imag,                // указатель на общий кадр
    std::mutex* mtx,              // мьютекс для синхронизации доступа к кадру
    std::condition_variable* cv_src // condition_variable для ожидания нового кадра
) {

    // Инициализация WinSock
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    // TCP-сокет
    SOCKET sock = INVALID_SOCKET;

    // Структура адреса сервера
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);              // порт в сетевом порядке
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr); // IP-адрес сервера

    // Попытка подключиться к серверу (с повтором при ошибке)
    while (sock == INVALID_SOCKET && running) {
        sock = socket(AF_INET, SOCK_STREAM, 0);

        // Если соединение не удалось — закрываем сокет и ждём
        if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            closesocket(sock);
            sock = INVALID_SOCKET;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    // Основной цикл работы потока
    while (running) {
        // Захватываем мьютекс
        std::unique_lock<std::mutex> lk(*mtx);

        // Ждём появления нового кадра или сигнала остановки
        cv_src->wait(lk, [&] { return !imag->empty() || !running; });

        // Если работа остановлена — выходим из цикла
        if (!running) break;

        // Клонируем кадр, чтобы работать с ним вне мьютекса
        cv::Mat local = imag->clone();

        // Освобождаем мьютекс
        lk.unlock();

        // Отправляем кадр по TCP
        send_frame(sock, local);
    }

    // Закрытие сокета
    closesocket(sock);

    // Очистка WinSock
    WSACleanup();
}


// =================== MAIN ===================

int main() {
    SetProcessDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
    );

    std::thread t1(CaptureThread);
    //std::thread t2(ModifikatorThread1);
    //std::thread t3(ModifikatorThread2);

    std::thread t4(DisplayThread1);
    //std::thread t5(DisplayThread2);
    //std::thread t6(DisplayThread3);

    std::thread t7(TcpThread, port1, &frame_0, &m_raw, &cv_raw);
    //std::thread t8(TcpThread, port2, &frame_dop1, &m_dop1, &cv_dop1);
    //std::thread t9(TcpThread, port3, &frame_dop2, &m_dop2, &cv_dop2);

    t1.join();
    //t2.join();
    //t3.join();
    t4.join();
    //t5.join();
    //t6.join();
    t7.join();
    //t8.join();
    //t9.join();

    return 0;
}
