# HexagonRPC 配置指南

## hexagonrpc.json — HexagonFS 路径映射配置

hexagonrpcd 通过 JSON 配置文件定义自定义的虚拟路径 → 物理路径映射。

### 文件位置

```
{device_dir}/hexagonrpc.json
```

其中 `{device_dir}` 由 `-R` 参数指定或自动检测（见下方）。

### 自动设备目录检测

当 `-R` 未指定时，hexagonrpcd 读取以下文件自动猜测设备目录：

1. `/proc/device-tree/compatible` → 提取 SoC 名称（如 `sdm845`）
2. `/proc/device-tree/model` → 提取厂商名称（如 `SHIFT`）
3. `/proc/device-tree/compatible` → 提取设备代号（如 `axolotl`）

尝试路径：`/usr/share/qcom/{soc}/{vendor}/{device}/`

### JSON 格式

```json
{
    "root_path": "/usr/share/qcom/sdm845/SHIFT/axolotl",
    "mappings": [
        {
            "virtual_path": "/vendor/etc",
            "physical_path": "vendor/etc"
        },
        {
            "virtual_path": "/vendor/dsp",
            "physical_path": "dsp"
        },
        {
            "virtual_path": "/persist",
            "physical_path": "persist"
        },
        {
            "virtual_path": "/sys/devices/soc0",
            "physical_path": "socinfo"
        }
    ]
}
```

### 字段说明

| 字段 | 类型 | 说明 |
|------|------|------|
| `root_path` | string | HexagonFS 根目录的绝对路径。所有物理路径以其为基准 |
| `mappings` | array | 路径映射列表 |
| `mappings[].virtual_path` | string | DSP 端访问的虚拟路径 |
| `mappings[].physical_path` | string | 相对于 `root_path` 的物理子路径 |

### 默认映射

如果 JSON 文件中未定义 `mappings`，使用以下内置默认映射：

| 虚拟路径 | 物理路径 |
|----------|----------|
| `/vendor/etc` | `{root}/vendor/etc/` |
| `/vendor/dsp` | `{root}/dsp/` |
| `/persist` | `{root}/persist/` |
| `/sys/devices/soc0` | `{root}/socinfo/` |

### 别名（硬链接）

HexagonFS 自动创建以下别名路径，无需配置：

| 别名 | → 实际路径 |
|------|-----------|
| `/vendor/` | `/system/vendor/`（互为别名） |
| `/persist/` | `/mnt/vendor/persist/`（互为别名） |
| `/vendor/etc/acdbdata/` | `/acdb/` |
| `/vendor/etc/sensors/` | `/sensors/` |

## 运行示例

```bash
# 1. 准备目录和配置
mkdir -p /usr/share/qcom/sdm845/SHIFT/axolotl
cat > /usr/share/qcom/sdm845/SHIFT/axolotl/hexagonrpc.json << 'JSON'
{
    "root_path": "/usr/share/qcom/sdm845/SHIFT/axolotl",
    "mappings": [
        {"virtual_path": "/vendor/etc", "physical_path": "vendor/etc"},
        {"virtual_path": "/vendor/dsp", "physical_path": "dsp"},
        {"virtual_path": "/persist", "physical_path": "persist"},
        {"virtual_path": "/sys/devices/soc0", "physical_path": "socinfo"}
    ]
}
JSON

# 2. 放置 DSP 固件文件（Android 设备上提取）
mkdir -p /usr/share/qcom/sdm845/SHIFT/axolotl/{dsp,acdb,sensors,persist,socinfo}

# 3. 启动 hexagonrpcd
hexagonrpcd -f /dev/fastrpc-adsp -R /usr/share/qcom/sdm845/SHIFT/axolotl
```

## systemd 集成

安装后，5 个 systemd service 通过 udev 规则自动触发。service 文件在 `data/` 目录中：

```
hexagonrpcd-adsp-rootpd.service
hexagonrpcd-adsp-audiopd.service
hexagonrpcd-adsp-sensorspd.service
hexagonrpcd-cdsp.service
hexagonrpcd-sdsp.service
```

每个 service 的 `ExecStart` 通过 Meson 的 `@BINDIR@` 占位符指向正确的 hexagonrpcd 路径和设备节点。
