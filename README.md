<img width="1583" height="998" alt="Снимок экрана 2026-09-11 215209" src="https://github.com/user-attachments/assets/be9f657e-68a9-4929-b885-1cdb1889a2c3" />
<img width="1575" height="986" alt="Снимок экрана 2026-09-11 215227" src="https://github.com/user-attachments/assets/5f74cefb-8faf-4c25-a1cc-fc895b633bbe" />

# IgorOS Information
IgorOS - это самостоятельная система с нуля, использует загрузчик Limine, благодаря чему у этой ОС есть совместимость с BIOS/UEFI. Система работает на архитектуре x64

Стек системы: компилятор MSYS2 | загрузчик QEMU

# IgorOS STart
1. Установите Sources проекта
2. Установите MSYS2 и QEMU на ваш компьютер
3. Проверьте работу каждого компонента, проверкой версии
Пример:
```
pacman -Syu
pacman -S make mingw-w64-ucrt-x86_64-gcc nasm
```
4. Устанвите зависимости (можно командоый выше)
5. Запустите ОС командой:
```
make
```
