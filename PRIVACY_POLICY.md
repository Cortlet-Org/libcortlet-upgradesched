# Privacy Policy

**Effective Date:** August 19, 2026  
**Project:** `libcortlet-upgradesched`  
**Maintainer:** Cortlet

## Overview

`libcortlet-upgradesched` is a software library for task scheduling.

The library itself is designed to operate locally as part of applications that integrate it. It does not, by default, collect, transmit, sell, or share personal information.

This Privacy Policy explains how privacy is handled by the `libcortlet-upgradesched` project.

## Information We Collect

The `libcortlet-upgradesched` library does not intentionally collect personal information.

The library does not include built-in functionality for collecting:

- Names
- Email addresses
- Phone numbers
- Physical addresses
- Account credentials
- Payment information
- Precise location information
- Advertising identifiers
- Browsing history
- User profiles

## Scheduler Statistics

`libcortlet-upgradesched` may expose runtime scheduler statistics to the application using the library.

These statistics may include:

- Number of submitted tasks
- Number of completed tasks
- Number of rejected submissions
- Number of outstanding tasks
- Work-steal attempts
- Successful work steals
- Failed work steals
- Priority fairness activations
- Sleeping worker counts
- Scheduler configuration information

These statistics describe scheduler activity and are not intended to contain personal information.

The library does not automatically transmit these statistics anywhere.

## Task Data

Applications may provide arbitrary pointers or data to tasks submitted to the scheduler.

`libcortlet-upgradesched` does not inspect, interpret, store permanently, or transmit the contents of application-provided task data.

The application using the library is responsible for determining what information is passed into scheduled tasks and how that information is handled.

## Network Activity

`libcortlet-upgradesched` does not require network access for normal scheduler operation.

The library does not automatically:

- Connect to Cortlet servers
- Contact third-party services
- Upload scheduler statistics
- Send analytics
- Send telemetry
- Send crash reports
- Download advertisements
- Track users across applications or websites

## Telemetry and Analytics

The library does not include built-in telemetry or analytics collection.

Applications that integrate `libcortlet-upgradesched` may independently implement their own telemetry, analytics, logging, or monitoring systems.

Such systems are controlled by the application developer and are not part of `libcortlet-upgradesched`.

## Logs

The library may be used in applications that create logs.

Any logging performed by the integrating application is controlled by that application.

Developers should avoid placing sensitive or personal information into logs unless necessary and should protect such logs appropriately.

## Data Storage

`libcortlet-upgradesched` does not provide persistent storage for personal information.

Scheduler state normally exists only in memory while the scheduler is running.

The library does not automatically create databases, user profiles, tracking files, or cloud records.

## Data Sharing

Cortlet does not receive personal information merely because an application uses `libcortlet-upgradesched`.

The library does not automatically share data with:

- Advertisers
- Analytics companies
- Data brokers
- Social networks
- Cloud providers
- Other third parties

## Selling of Personal Information

`libcortlet-upgradesched` does not sell personal information.

The library does not contain advertising or data-broker functionality.

## Third-Party Dependencies

`libcortlet-upgradesched` may depend on third-party open-source software libraries.

These dependencies are used to provide technical functionality such as synchronization, queues, platform integration, and runtime support.

The use of an open-source dependency does not mean that application data is automatically transmitted to the dependency's maintainers.

Developers should review the licenses and behavior of dependencies they include in production deployments.

## Applications Using This Library

This Privacy Policy applies to the `libcortlet-upgradesched` project itself.

Applications that use this library may collect or process information independently.

Developers distributing applications that use `libcortlet-upgradesched` are responsible for:

- Providing their own privacy policy when required
- Explaining what information their application collects
- Obtaining any legally required consent
- Protecting user information
- Complying with applicable privacy laws and regulations
- Configuring their own analytics, logging, networking, and storage systems appropriately

## Security

The project is designed with a focus on safe scheduling and interoperability between Rust and C.

However, no software can guarantee absolute security.

Applications using `libcortlet-upgradesched` remain responsible for securely managing:

- Memory passed into tasks
- Application credentials
- User data
- Network communications
- Persistent storage
- Logs
- Access controls
- Authentication systems

## Children's Privacy

`libcortlet-upgradesched` is a general-purpose software library and does not intentionally collect personal information from children or any other users.

Applications directed toward children are responsible for complying with applicable children's privacy laws.

## International Use

Because `libcortlet-upgradesched` does not normally collect or transmit personal information, the library itself does not normally perform international transfers of user data.

Applications integrating the library may process data internationally depending on their own architecture and services.

## Open-Source Distribution

`libcortlet-upgradesched` may be distributed as open-source software.

Anyone may inspect the source code to understand how the scheduler operates and what information it processes.

The project's license is provided separately in the repository.

## Changes to This Privacy Policy

This Privacy Policy may be updated as the project evolves.

Changes may be made when:

- New functionality is added
- Data-handling behavior changes
- New services are introduced
- Legal or regulatory requirements change

Material changes should be reflected by updating this document and its effective date.

## Contact

For questions about this Privacy Policy or the `libcortlet-upgradesched` project, contact the project maintainers through the official Cortlet repository or organization.

## Summary

By default, `libcortlet-upgradesched`:

- Does not collect personal information
- Does not transmit personal information
- Does not sell personal information
- Does not include advertising
- Does not perform user tracking
- Does not include built-in telemetry
- Does not require network access
- Keeps scheduler state locally in memory
- Exposes technical scheduler statistics only to the integrating application

Applications using the library remain responsible for their own privacy practices.