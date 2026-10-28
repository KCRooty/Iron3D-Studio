# Instalador de Iron3D Studio (Fase 4)

Script de [Inno Setup 6](https://jrsoftware.org/isinfo.php) que empaqueta
Iron3D Studio en un instalador de Windows con el mismo flujo de pantallas
que el instalador real de Cinema 4D R19 (9 capturas de referencia dadas por
Core), **excepto la pagina de registro/serial, excluida a proposito**.

## Pasos del asistente

1. Bienvenida.
2. Informacion previa (`installer_readme.txt`): requisitos + contacto de
   soporte -- equivalente a la segunda pantalla del README de C4D R19.
3. Licencia (EULA, `../LICENSE-MIT`) con checkbox "He leido y acepto".
4. **Backend de render** (pagina propia, `[Code]` en `iron3d_setup.iss`):
   elegir Vulkan (por defecto) u OpenGL -- escribe directo a
   `preferences.cfg` en la carpeta de instalacion, mismo formato que lee
   `core/Preferences.cpp` en tiempo de ejecucion. Es el equivalente honesto
   de la pagina "tipo de instalacion" de C4D para un producto de un solo
   ejecutable, y cumple el pedido de Core de poder elegir el backend "en la
   instalacion" ademas de en Preferencias dentro de la app.
5. Componentes (nucleo obligatorio + escenas de ejemplo + documentacion).
6. Carpeta de destino (`{autopf}\Static Forge\Iron3D Studio` por defecto).
7. Grupo del menu Inicio + icono de escritorio opcional.
8. Instalando.
9. Fin, con opcion de lanzar Iron3D Studio.

## Compilar el instalador

Requiere una build **Release** ya hecha (`cmake --build build --config
Release --target Iron3DStudio`) antes de compilar el `.iss` -- el script
empaqueta `build\bin\Release\Iron3DStudio.exe` y su carpeta `shaders\`.

```powershell
cd installer
& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" iron3d_setup.iss
```

El `.exe` resultante cae en `installer\output\` (ignorado por git, igual que
cualquier otro `.exe` del repo).

## Branding (clean-room)

`assets/wizard_image.bmp`, `assets/wizard_small.bmp` y `assets/setup_icon.ico`
son originales (generados a partir de `assets/icon.png`, el icono I3DS de
Core, sobre un fondo oscuro propio) -- nada de arte real de Maxon. La regla
clean-room del proyecto cubre tambien el instalador: se replica la
ESTRUCTURA del asistente de C4D, nunca sus imagenes.
