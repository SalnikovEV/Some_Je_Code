import threading
import time
import keyboard   # pip install keyboard

# Инициализация данных
views = list(range(10))          # 0–9 - список допустимых цифр
dop_f = ["s", "q", "p"]          # список дополнительных функций (доп. клавиши)

# Переменные для хранения текущих индексов (состояния программы)
index_views = -1                 # текущая нажатая цифра (-1 - ничего не нажато)
index_dop = -1                   # текущая нажатая доп. клавиша

# Блокировка для безопасного доступа к общим переменным из разных потоков
lock = threading.Lock()
# Флаг работы программы (при False все потоки завершаются)
run = True

# Поток 1 — обработка цифровых клавиш (views)
def activ_view_key():
    global index_views, run  # объявляем глобальные переменные для изменения
    while run:  # бесконечный цикл, пока run = True
        event = keyboard.read_event()  # ждём нажатия клавиши (блокирующий вызов)
        
        # Обрабатываем только события нажатия клавиш (игнорируем отпускание)
        if event.event_type == keyboard.KEY_DOWN:
            # Проверяем, что нажата цифра
            if event.name.isdigit():
                val = int(event.name)  # преобразуем строку в число
                # Проверяем, что цифра входит в допустимый диапазон
                if val in views:
                    with lock:  # безопасный доступ к общей переменной
                        index_views = val  # обновляем текущую цифру

# Поток 2 — обработка дополнительных клавиш и управляющих команд
def dop_key():
    global index_dop, run  # объявляем глобальные переменные для изменения

    while run:  # бесконечный цикл, пока run = True
        event = keyboard.read_event()  # ждём нажатия клавиши
        
        # Обрабатываем только события нажатия клавиш
        if event.event_type == keyboard.KEY_DOWN:
            print(f"[KEY] Нажата клавиша: '{event.name}'")
            
            # Обработка дополнительных функциональных клавиш
            if event.name in dop_f:
                with lock:  # безопасный доступ к общей переменной
                    index_dop = dop_f.index(event.name)  # находим индекс клавиши в списке
                    print(f"[DOP] символ='{event.name}', index_dop={index_dop}")
            
            # Обработка команды выхода
            if event.name == "esc":
                run = False  # устанавливаем флаг завершения
                print("Выход...")
                break  # выходим из цикла в этом потоке

# MAIN - точка входа в программу
if __name__ == "__main__":
    # Создаём два потока с демоническим флагом (daemon=True)
    # Демонические потоки завершаются автоматически при завершении главного потока
    t1 = threading.Thread(target=activ_view_key, daemon=True)
    t2 = threading.Thread(target=dop_key, daemon=True)

    # Запускаем потоки
    t1.start()
    t2.start()

    # Выводим инструкцию для пользователя
    print("Нажимай:")
    print("  цифры 0–9  -> views")
    print("  s q p      -> dop")
    print("  ESC        -> выход")
    print()  # пустая строка для читаемости

    # Главный цикл программы - отображает текущее состояние
    while run:
        # Безопасно читаем текущие значения из общих переменных
        with lock:
            v = index_views  # копируем значение для локального использования
            d = index_dop    # копируем значение для локального использования
        
        # Отображаем текущее состояние
        print(f"[MAIN] index_views = {v}, index_dop = {d}")
        
        # Пауза, чтобы не перегружать консоль и CPU
        time.sleep(0.2)

    # Завершающее сообщение
    print("Программа завершена")
