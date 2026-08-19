use std::io;
use std::thread;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct CpuInfo {
    logical_id: usize,
}

impl CpuInfo {
    #[must_use]
    pub(crate) fn logical_id(&self) -> usize {
        self.logical_id
    }
}

#[derive(Debug, Clone)]
pub(crate) struct Topology {
    cpus: Vec<CpuInfo>,
}

impl Topology {
    #[must_use]
    pub(crate) fn detect() -> Self {
        let count = thread::available_parallelism()
            .map(|value| value.get())
            .unwrap_or(1);

        let cpus = (0..count)
            .map(|logical_id| CpuInfo { logical_id })
            .collect();

        Self { cpus }
    }

    #[must_use]
    pub(crate) fn cpu_count(&self) -> usize {
        self.cpus.len()
    }

    #[must_use]
    pub(crate) fn cpus(&self) -> &[CpuInfo] {
        &self.cpus
    }

    #[must_use]
    pub(crate) fn cpu_for_worker(
        &self,
        worker_id: usize,
    ) -> Option<CpuInfo> {
        if self.cpus.is_empty() {
            return None;
        }

        Some(
            self.cpus[
                worker_id % self.cpus.len()
                ],
        )
    }
}

#[cfg(target_os = "linux")]
pub(crate) fn pin_current_thread(
    cpu: CpuInfo,
) -> io::Result<()> {
    unsafe {
        let mut set: libc::cpu_set_t =
            std::mem::zeroed();

        libc::CPU_ZERO(&mut set);
        libc::CPU_SET(
            cpu.logical_id,
            &mut set,
        );

        let result = libc::pthread_setaffinity_np(
            libc::pthread_self(),
            std::mem::size_of::<libc::cpu_set_t>(),
            &set,
        );

        if result != 0 {
            return Err(
                io::Error::from_raw_os_error(result),
            );
        }
    }

    Ok(())
}

#[cfg(not(target_os = "linux"))]
pub(crate) fn pin_current_thread(
    _cpu: CpuInfo,
) -> io::Result<()> {
    Err(
        io::Error::new(
            io::ErrorKind::Unsupported,
            "CPU affinity is only supported on Linux",
        ),
    )
}