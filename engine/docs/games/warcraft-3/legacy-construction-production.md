# Legacy construction and production

Monolith custom buildings use `build == self` as their legacy construction
marker without an active construction component. Treating that marker as a
training queue made `unit_add_build_queue` traverse the same building forever
when the original campaign AI called SetProduce.

The authoritative train-command state now returns disabled while this marker
exists. Both direct training and AI production use the same check, so rejected
orders allocate no trainee and spend no resources. The existing construction
lifecycle clears the marker; subsequent AI training works normally. Ordinary
occupied queues remain available and retain the existing AI balancing policy.

A bounded ROC/TFT red regression checks the command state before calling the
formerly infinite append path. The fixed test also runs direct training, AI
rejection, resource/entity invariants and successful post-construction
production. A serial stack sample and temporary diagnostic build located the
original cycle; the diagnostic guard is excluded from production.
