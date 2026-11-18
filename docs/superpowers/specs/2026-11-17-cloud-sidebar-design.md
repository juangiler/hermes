# Separate Cloud sidebar section

## Approved intent

Keep Network for native connections (including direct WebDAV) and put all
rclone remotes, including rclone WebDAV, in a separate Cloud section.
Connection, mounting, and reconnection behavior must not change.

## Approach

Add a Cloud header alongside Network, with independent collapse state.
Move the existing rclone rows and configuration/refresh menu actions there;
the Cloud header's plus button opens rclone configuration.
Keep Cloud visible when rclone is available, even without configured remotes.
Keep the existing behavior of hiding cloud controls when rclone is unavailable.

Keeping rclone under Network would not meet the requested separation.
Classifying only WebDAV separately would leave other rclone remotes inconsistent.

## Constraints and checks

Use existing C sidebar, input, rendering, and settings patterns; no dependencies
or connection backend changes. Persist independent collapse state as
`cloud_collapsed`, defaulting to expanded for existing configurations.
Retain duplicate suppression for discovered rclone mounts.
Extend the existing m10 suite to cover WebDAV categorization, independent
collapse, header controls, settings persistence, and unavailable rclone.
Update settings documentation, connection-form guidance, and the changelog.

The user approved the Network/Cloud separation and independent heading.
Header visibility and settings naming above are implementation choices.
