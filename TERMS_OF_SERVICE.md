# Terms of Service

**Effective Date:** August 19, 2026  
**Project:** `libcortlet-upgradesched`  
**Maintainer:** Cortlet

## 1. Overview

These Terms of Service govern the use, distribution, integration, modification, and operation of `libcortlet-upgradesched`.

`libcortlet-upgradesched` is a software library that provides task scheduling functionality through a Rust implementation and a C-compatible ABI.

By using, integrating, modifying, or distributing `libcortlet-upgradesched`, you agree to these Terms of Service.

## 2. Open-Source License

`libcortlet-upgradesched` is distributed under the MIT License.

The MIT License grants broad rights to:

- Use the software
- Copy the software
- Modify the software
- Merge the software
- Publish the software
- Distribute the software
- Sublicense the software
- Sell copies of the software

These rights are subject to the conditions stated in the project's `LICENSE` file.

If these Terms of Service conflict with rights granted by the MIT License, the MIT License controls the licensing rights granted with the software.

## 3. Use of the Software

You may use `libcortlet-upgradesched` for lawful purposes, including:

- Personal projects
- Commercial applications
- Open-source applications
- Research
- Education
- Server software
- Desktop applications
- Embedded or systems software
- Developer tools
- Other software products

You are responsible for ensuring that your use of the software complies with applicable laws, regulations, licenses, and contractual obligations.

## 4. Prohibited Use

You may not use the project in a way that violates applicable law.

You are responsible for the behavior of applications that integrate or modify `libcortlet-upgradesched`.

Cortlet does not control applications created by third parties using this library.

## 5. No Hosted Service

`libcortlet-upgradesched` is primarily a software library and is not, by itself, a hosted online service.

The library does not require a Cortlet account to operate.

The library does not normally provide:

- User accounts
- Cloud storage
- Authentication services
- Hosted databases
- Payment processing
- Advertising services
- Analytics services
- Remote task execution services

Applications built using the library may independently provide such functionality.

## 6. Application Responsibilities

Developers integrating `libcortlet-upgradesched` are responsible for their own applications.

This includes responsibility for:

- Application security
- User authentication
- Authorization
- Memory safety outside the library
- Data handling
- Logging
- Networking
- Storage
- Privacy compliance
- Error handling
- Backup systems
- Availability
- Application-specific testing

The library does not automatically make an integrating application secure, reliable, or compliant.

## 7. Task Data and Memory

The scheduler allows applications to submit task functions and associated arguments.

The caller is responsible for ensuring that:

- Task function pointers are valid
- Task argument pointers remain valid for the required lifetime
- Shared data is synchronized correctly
- Data is not freed while still in use
- C ABI contracts are followed
- Scheduler and task-group handles are used correctly

Incorrect pointer management or concurrent access may cause undefined behavior in the integrating application.

## 8. Concurrency Responsibilities

`libcortlet-upgradesched` is designed for concurrent scheduling workloads.

Applications remain responsible for correctly managing concurrency in their own code.

In particular:

- Application task callbacks must manage shared state safely
- `cortlet_sched_destroy()` must not race with another operation using the same scheduler handle
- Destroyed scheduler handles must not be reused
- Destroyed task-group handles must not be reused
- Application-owned memory passed into tasks must remain valid until no longer required

## 9. Scheduler Behavior

The scheduler may provide functionality including:

- Worker threads
- Work stealing
- Priority scheduling
- Fairness mechanisms
- Task groups
- Bounded outstanding-task capacity
- Scheduler statistics
- Scheduler introspection
- Graceful shutdown

Specific scheduling order may depend on:

- Worker availability
- Concurrent submissions
- Task priorities
- Local worker queues
- Work stealing
- Scheduler fairness behavior
- Operating system scheduling
- CPU topology
- Runtime timing

Unless explicitly documented otherwise, applications must not depend on an exact global execution order between concurrent tasks.

## 10. Task Priorities

Task priorities are scheduling hints and policy inputs.

The scheduler may prioritize HIGH work over NORMAL and LOW work while also applying fairness mechanisms intended to reduce starvation.

Priority does not guarantee:

- Immediate execution
- Real-time deadlines
- Exact execution order
- Exclusive CPU access
- Operating-system scheduling priority

Applications requiring hard real-time guarantees should not assume that `libcortlet-upgradesched` provides them.

## 11. Performance

Performance results depend on many factors, including:

- CPU architecture
- Worker count
- Compiler optimization
- Operating system
- Build configuration
- Task duration
- Memory bandwidth
- CPU topology
- Concurrent system load
- Application behavior

Benchmark numbers published by the project are examples and are not guarantees of future or application-specific performance.

## 12. Statistics and Introspection

Scheduler statistics and introspection values are snapshots.

Values may change immediately after they are read.

Statistics may include:

- Submitted tasks
- Completed tasks
- Rejected submissions
- Work-steal attempts
- Successful steals
- Failed steals
- Fairness activations
- Outstanding tasks
- Sleeping workers

These values are intended for diagnostics, monitoring, and development.

They should not be treated as synchronized transactional state unless explicitly documented.

## 13. API and ABI Compatibility

The project attempts to provide a stable C ABI within compatible releases.

However, compatibility guarantees apply only where explicitly documented.

Developers should review release notes before upgrading.

Major-version releases may introduce incompatible changes.

Public ABI changes may include changes to:

- Struct layouts
- Function signatures
- Result codes
- Configuration structures
- Enumeration representations
- Scheduler behavior

Applications should be rebuilt and tested when upgrading between major versions.

## 14. Modifications

You may modify the project under the terms of the MIT License.

Modified versions are not automatically supported or endorsed by Cortlet.

If you distribute a modified version, you are responsible for:

- Testing your modifications
- Maintaining ABI compatibility if desired
- Updating documentation where necessary
- Complying with applicable licenses
- Clearly distinguishing your modifications when appropriate

## 15. Third-Party Software

The project may use third-party open-source dependencies.

Those dependencies may have their own licenses and terms.

You are responsible for reviewing applicable third-party licenses when redistributing the software.

## 16. Availability

Cortlet does not guarantee that:

- Development will continue indefinitely
- Every issue will be fixed
- Every platform will remain supported
- Every feature will remain unchanged
- New releases will occur on a particular schedule

The project may be changed, suspended, archived, or discontinued.

## 17. Support

Unless separately agreed in writing, use of `libcortlet-upgradesched` does not include guaranteed technical support.

Support may be provided through:

- Repository issues
- Documentation
- Discussions
- Community contributions
- Future support channels

Response times and issue resolution are not guaranteed.

## 18. Security Issues

Users and developers are encouraged to responsibly report security vulnerabilities.

Do not knowingly publish sensitive exploit details before maintainers have had a reasonable opportunity to investigate where responsible disclosure is appropriate.

Applications using the library remain responsible for their own security architecture.

## 19. Privacy

Use of the project is also subject to the project's `PRIVACY_POLICY.md`.

By default, `libcortlet-upgradesched` does not intentionally collect or transmit personal information.

Applications integrating the library may independently collect or process information and are responsible for their own privacy practices.

## 20. Disclaimer of Warranty

The software is provided under the MIT License on an **"AS IS"** basis.

To the maximum extent permitted by law, the software is provided without warranties of any kind, whether express or implied.

This includes, without limitation, warranties of:

- Merchantability
- Fitness for a particular purpose
- Non-infringement
- Availability
- Reliability
- Security
- Performance
- Error-free operation

## 21. Limitation of Liability

To the maximum extent permitted by law, Cortlet, project maintainers, contributors, and copyright holders will not be liable for claims, damages, losses, or other liability arising from or related to the software.

This may include:

- Data loss
- Service interruption
- Lost profits
- Hardware damage
- Application failures
- Security incidents
- Business interruption
- Indirect damages
- Incidental damages
- Consequential damages

The limitations stated in the MIT License also apply.

## 22. Indemnification

To the extent permitted by applicable law, you are responsible for claims arising from your own application, modifications, distribution, misuse, or unlawful use of the software.

You are responsible for ensuring that products built with `libcortlet-upgradesched` comply with applicable requirements.

## 23. Export and Legal Compliance

You are responsible for complying with applicable:

- Export laws
- Import laws
- Sanctions
- Trade restrictions
- Software regulations
- Industry requirements

Cortlet does not provide legal advice regarding use of the project in a particular jurisdiction or industry.

## 24. Changes to These Terms

These Terms of Service may be updated as the project evolves.

Changes may be made to reflect:

- New functionality
- New releases
- Legal requirements
- Security practices
- Project governance
- Distribution changes

The effective date at the top of this document should be updated when material changes are made.

## 25. Governing Terms

The MIT License remains the primary software license governing rights to use, copy, modify, and distribute the project.

These Terms of Service provide additional information about use and project expectations but are not intended to remove rights granted under the MIT License.

## 26. Contact

Questions about these Terms of Service may be directed to the maintainers through the official Cortlet organization or the official `libcortlet-upgradesched` repository.

## 27. Summary

By using `libcortlet-upgradesched`, you acknowledge that:

- The project is open-source software
- The software is licensed under the MIT License
- The software is provided without warranty
- You are responsible for your application and task data
- Exact scheduling order is not guaranteed
- Priority scheduling is not a hard real-time guarantee
- Benchmark results are not performance guarantees
- Public APIs and ABI should be reviewed when upgrading
- Applications using the library are responsible for their own security and privacy practices