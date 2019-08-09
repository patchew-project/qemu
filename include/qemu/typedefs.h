#ifndef QEMU_TYPEDEFS_H
#define QEMU_TYPEDEFS_H

/*
 * Incomplete struct types
 * Please keep this list in case-insensitive alphabetical order.
 */
typedef struct AdapterInfo AdapterInfo;
typedef struct AddressSpace AddressSpace;
typedef struct AioContext AioContext;
typedef struct AnnounceTimer AnnounceTimer;
typedef struct BdrvDirtyBitmap BdrvDirtyBitmap;
typedef struct BdrvDirtyBitmapIter BdrvDirtyBitmapIter;
typedef struct BlockBackend BlockBackend;
typedef struct BlockBackendRootState BlockBackendRootState;
typedef struct BlockDriverState BlockDriverState;
typedef struct BusClass BusClass;
typedef struct BusState BusState;
typedef struct Chardev Chardev;
typedef struct CompatProperty CompatProperty;
typedef struct CoMutex CoMutex;
typedef struct CPUAddressSpace CPUAddressSpace;
typedef struct CPUState CPUState;
typedef struct DeviceListener DeviceListener;
typedef struct DeviceState DeviceState;
typedef struct DirtyBitmapSnapshot DirtyBitmapSnapshot;
typedef struct DisplayChangeListener DisplayChangeListener;
typedef struct DriveInfo DriveInfo;
typedef struct Error Error;
typedef struct EventNotifier EventNotifier;
typedef struct FlatView FlatView;
typedef struct FWCfgEntry FWCfgEntry;
typedef struct FWCfgIoState FWCfgIoState;
typedef struct FWCfgMemState FWCfgMemState;
typedef struct FWCfgState FWCfgState;
typedef struct HVFX86EmulatorState HVFX86EmulatorState;
typedef struct I2CBus I2CBus;
typedef struct I2SCodec I2SCodec;
typedef struct IOMMUMemoryRegion IOMMUMemoryRegion;
typedef struct ISABus ISABus;
typedef struct ISADevice ISADevice;
typedef struct IsaDma IsaDma;
typedef struct MACAddr MACAddr;
typedef struct MachineClass MachineClass;
typedef struct MachineState MachineState;
typedef struct MemoryListener MemoryListener;
typedef struct MemoryMappingList MemoryMappingList;
typedef struct MemoryRegion MemoryRegion;
typedef struct MemoryRegionCache MemoryRegionCache;
typedef struct MemoryRegionSection MemoryRegionSection;
typedef struct MigrationIncomingState MigrationIncomingState;
typedef struct MigrationState MigrationState;
typedef struct Monitor Monitor;
typedef struct MonitorDef MonitorDef;
typedef struct MSIMessage MSIMessage;
typedef struct NetClientState NetClientState;
typedef struct NetFilterState NetFilterState;
typedef struct NICInfo NICInfo;
typedef struct NodeInfo NodeInfo;
typedef struct NumaNodeMem NumaNodeMem;
typedef struct ObjectClass ObjectClass;
typedef struct PCIBridge PCIBridge;
typedef struct PCIBus PCIBus;
typedef struct PCIDevice PCIDevice;
typedef struct PCIEAERErr PCIEAERErr;
typedef struct PCIEAERLog PCIEAERLog;
typedef struct PCIEAERMsg PCIEAERMsg;
typedef struct PCIEPort PCIEPort;
typedef struct PCIESlot PCIESlot;
typedef struct PCIExpressDevice PCIExpressDevice;
typedef struct PCIExpressHost PCIExpressHost;
typedef struct PCIHostDeviceAddress PCIHostDeviceAddress;
typedef struct PCIHostState PCIHostState;
typedef struct PCMachineState PCMachineState;
typedef struct PostcopyDiscardState PostcopyDiscardState;
typedef struct Property Property;
typedef struct PropertyInfo PropertyInfo;
typedef struct QBool QBool;
typedef struct QDict QDict;
typedef struct QEMUBH QEMUBH;
typedef struct QemuConsole QemuConsole;
typedef struct QEMUFile QEMUFile;
typedef struct QemuLockable QemuLockable;
typedef struct QemuMutex QemuMutex;
typedef struct QemuOpt QemuOpt;
typedef struct QemuOpts QemuOpts;
typedef struct QemuOptsList QemuOptsList;
typedef struct QEMUSGList QEMUSGList;
typedef struct QemuSpin QemuSpin;
typedef struct QEMUTimer QEMUTimer;
typedef struct QEMUTimerListGroup QEMUTimerListGroup;
typedef struct QJSON QJSON;
typedef struct QList QList;
typedef struct QNull QNull;
typedef struct QNum QNum;
typedef struct QObject QObject;
typedef struct QString QString;
typedef struct RAMBlock RAMBlock;
typedef struct Range Range;
typedef struct SHPCDevice SHPCDevice;
typedef struct SSIBus SSIBus;
typedef struct VirtIODevice VirtIODevice;
typedef struct Visitor Visitor;

/*
 * Pointer types
 * Such typedefs should be limited to cases where the typedef's users
 * are oblivious of its "pointer-ness".
 * Please keep this list in case-insensitive alphabetical order.
 */
typedef struct IRQState *qemu_irq;

/*
 * Function types
 */
typedef void SaveStateHandler(QEMUFile *f, void *opaque);
typedef int LoadStateHandler(QEMUFile *f, void *opaque, int version_id);
typedef void (*qemu_irq_handler)(void *opaque, int n, int level);

#endif /* QEMU_TYPEDEFS_H */
