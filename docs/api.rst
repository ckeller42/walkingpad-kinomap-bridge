API reference
=============

Generated from the headers by Doxygen and pulled in by Breathe, so these pages
cannot drift from the code. Coverage is enforced by a ratchet in CI
(``tools/doxygen_ratchet.py``): the undocumented-symbol count may fall but never
rise, so modules are documented as they are touched.

The split below is the project's own architecture. Everything under
**Protocol and policy** is Arduino-free and unit-tested on the host; everything
under **Device adapters** needs real hardware and is therefore verified on the
bench instead.

Protocol and policy
-------------------

WiLink - the belt's own protocol
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Framing, CRC and the command set of the KingSmith WalkingPad R1 Pro.
See :doc:`protocol/reference` for the measured wire format.

.. doxygennamespace:: wilink
   :members:

FTMS - what the phone speaks
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Bluetooth Fitness Machine Service encoding: the characteristics Kinomap reads
and the Control Point it writes.

.. doxygennamespace:: ftms
   :members:

Policy - what the bridge decides
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Speed policy and bridge state: deadband, pacing, the start sequence, and the
arming gate. Every state-changing entry point returns an ``Outcome`` that the
host drains, so the decisions are testable without a belt.

.. doxygennamespace:: policy
   :members:

Belt - the abstraction over a real or simulated belt
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. doxygennamespace:: belt
   :members:

Screens
-------

Setup wizard
^^^^^^^^^^^^

First-run pairing flow. See :doc:`setup-wizard` for the rendered screens.

.. doxygennamespace:: wizard
   :members:

Developer menu
^^^^^^^^^^^^^^

The radio switches, Wi-Fi pairing and the protocol mode.

.. doxygennamespace:: devmenu
   :members:

Serial console
^^^^^^^^^^^^^^

Command parsing for the USB console. See :doc:`serial-console`.

.. doxygennamespace:: console
   :members:

Device adapters
---------------

Session logging
^^^^^^^^^^^^^^^

SD and UDP sinks. These exist because opening the serial port resets the
ESP32-S3, which makes the console unusable during a live session.

.. doxygennamespace:: logsink
   :members:
