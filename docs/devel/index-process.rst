.. _development_process:

QEMU Community Processes
------------------------

Notes about how to interact with the community and how and where to submit patches.

.. uml::
   :caption: QEMU Patch Review and Acceptance Flow

   @startuml
   skinparam roundCorner 8
   skinparam shadowing false
   skinparam defaultFontName monospace
   skinparam sequenceMessageAlign center

   actor "Developer" as dev
   participant "QEMU Community\n(qemu-devel)" as list
   actor "Subsystem\nMaintainer" as sub
   actor "QEMU\nMaintainer" as top

   dev -> dev: Develop & test feature

   loop Needs revision (v1, v2, ... vn)
       dev -> list: Post patch series to list
       note over list: Code review on list

       opt Review comments
           list -> dev: Feedback & change requests
           dev -> dev: Incorporate feedback
       end
       opt Review tags
           list -> dev: Tags (Reviewed-by, Acked-by, ...)
           dev -> dev: Apply tags
       end

       dev -> dev: Prepare n+1
   end

   group Patch acceptance & release
       list -> sub: Maintainer accepts some or all patches
       note over sub: Incorporates patches\nfrom other developers
       sub -> sub: Add Maintainer Sign-off
       sub -> sub: GIT branch foo/next
       sub -> list: Sign & Submit Pull Request
       list -> top: Pull Request delivered to list
       note over top: Pull Requests from\nother maintainers
       top -> top: Test and Merge Pull Reqs
       top -> top: qemu.git master
       top -> top: Tag Release
       top -> list: QEMU Release
   end
   @enduml

.. toctree::
   :maxdepth: 2

   code-of-conduct
   conflict-resolution
   maintainers
   style
   submitting-a-patch
   code-provenance
   trivial-patches
   stable-process
   submitting-a-pull-request
   secure-coding-practices
   rust
