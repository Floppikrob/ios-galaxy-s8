# Сравнение с открытыми драйверами — 2026-11-13

## Источники

- Samsung Exynos8895 DECON: https://github.com/jesec/android_kernel_samsung_universal8895/blob/642eaf533568f91a4d51392de066caced7a3a3e4/drivers/video/fbdev/exynos/dpu/decon_core.c (ветка tw-7.0, проверенный commit). Локальная копия work/reference-decon-core.c.
- Linux Panfrost: https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/panfrost/panfrost_job.c и panfrost_regs.h. Проверено на дату выше; master подвижен. Локальная копия заголовка work/reference-panfrost-regs.h.
- Другой порт iOS S8: https://github.com/Floppikrob/ios-galaxy-s8 . README сообщает CPU rendering и проблему старого1125×2436; это не описание нашего исправленного scaling mode.
- Android Mesa backend: https://github.com/JimVulkan/mali-panfrost . Работает через Android kbase /dev/mali0; готовую библиотеку нельзя просто загрузить в наш XNU/iOS как Metalдрайвер. Не доказательство работы G71 на нашем устройстве.

## Конкретные результаты сравнения

В Samsung decon_update_regs строки1760–1764 ждут fence каждого входного буфера. __decon_update_regs сначала ждёт завершение предыдущего shadow update и управляет trigger. Далее decon_update_regs ждёт vsync/vstatus/shadow update (1818–1824); старые буферы освобождаются и fence сигналится лишь в конце1871–1873. Это согласованный жизненный цикл, не произвольная задержка перед memcpy.
Наш путь постоянно пишет GPU в один физический framebuffer0xcc000000, из которого экран читает. Нет полного переключения scanoutбуферов/shadowcommit и release/acquirefenceпротокола iOSproducer. s8fb_owned1 уменьшает окно повторногоиспользования source, но не доказывает ownershipABI. Поэтому tearing/повторноеиспользование остаются обоснованными гипотезами, НЕ установленной причиной артефактов. Слепое ожидание DECON уже испытано: добавило9ms и не убрало артефакты; не возвращать его как готовое решение.
Panfrost panfrost_job_hw_submit использует START и END FLUSH_CLEAN_INVALIDATE и зависимости/fences по буферам. Сравнение panfrost_regs.h: start(3<<8), end(3<<12), startMMU(1<<10). Наш DreamMaliBlit.inc уже ставит эти три значения в JS_CONFIG_NEXT! Поэтому гипотеза «забыты флаги очистки GPUкэша» не подтверждена. CPUclean источника тоже уже есть. Нельзя обещать, что другие детали когерентности этим доказаны.

## План следующей реализации

1. Читать фактическую конфигурацию DECON/DPP/адреса source и shadow в текущем S-Boot режиме, не менять вслепую регистры из Android.
2. Подготовить отдельный backbuffer с правильным GPU/DECON mapping; GPU заканчивает масштабирование туда, потом аппаратный commit адреса с подтверждением, удерживать прежний буфер до завершения вывода. Обязательно проверить совместимость S-Boot commandmode/DSC/IOMMU и реальные ограничения адресов. Пока это проект изменения, не реализованная функция.
3. Отдельно сравнивать содержимое готового CPUисточника и результата GPU, чтобы отделить артефакт композиции от scanout. Пройденный статический selftest не проверяет гонки producer/consumer.
4. Только после корректной передачи кадров ускорять композицию (более полный графическийbackend). Само копирование не снижает стоимость CoreAnimation в backboardd.

Телефон не прошивался и не перезагружался в ходе поиска. GPU остаётся включённым, текущий образ3b135801... phoneTWRP. Настройки других устройств автоматически не переносились.
