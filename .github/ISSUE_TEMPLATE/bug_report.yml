name: 🐛 Проблема
title: '[Проблема] '
description: Сообщить о проблеме
labels: ['type: проблема', 'status: нуждается в сортировке']

body:
  - type: checkboxes
    id: checklist
    attributes:
      label: ⚠️ Чеклист
      description: Перед созданием нового Issue, удостоверьтесь что выполнили следующие пункты
      options:
        - label: Я искал решение проблемы в [README](https://github.com/JunDevX/CubeHavoc/blob/main/README.md) и среди [Issues](https://github.com/JunDevX/CubeHavoc/issues) и [Discussions](https://github.com/JunDevX/CubeHavoc/discussions)
        - label: Я скачал `CubeHavoc` из [релизов официального репозитория](https://github.com/JunDevX/CubeHavoc/releases)
        - label: Ознакомился с правилами создания Issue
          required: true

  - type: textarea
    id: description
    attributes:
      label: Опишите вашу проблему
      description: Чётко опишите проблему, с которой вы столкнулись
      placeholder: Описание проблемы
    validations:
      required: true

  - type: textarea
    id: environment
    attributes:
      label: Окружение / Характеристики системы (при желании)
      description: Укажите OC, видеокарту, провайдера или другие детали, если это влияет на проблему
      placeholder: Windows 10, AMD Radeon R3...

  - type: textarea
    id: version
    attributes:
      label: Версия CubeHavoc
      description: Версия или коммит CubeHavoc, на котором воспроизводится проблема
      placeholder: v1.0.0
    validations:
      required: true

  - type: textarea
    id: additions
    attributes:
      label: Дополнительные детали
      description: Логи, скриншоты или шаги для воспроизведения
