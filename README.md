# ODE lite kernel (ChisLink multiboot)

Minimal kernel for EZODE to work with ChisLink.

## Build

```
make          # -> litekernel.mb.gba
```

Uses devkitARM + libgba directly (devkitARM's `gba_rules` needs the absent
`gba_mb.specs`, so the rules are not included):

```
arm-none-eabi-gcc -mthumb -mthumb-interwork -mcpu=arm7tdmi -Os \
  -ffunction-sections -fdata-sections -fno-builtin -fno-common \
  -nostartfiles -specs=nosys.specs -Wl,--gc-sections \
  -T tools/mb/lite_mb.ld ... -lgba -lgcc
```

Used Chislink SDK from [ChisLink](https://github.com/ChisBread/ChisLink) project.