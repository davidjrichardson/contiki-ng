load("nashorn:mozilla_compat.js");
importPackage(java.io);
importPackage(java.util);
importPackage(org.contikios.cooja.util);
importPackage(org.contikios.cooja.interfaces);

var runNumber = %run%;

// Java types
var ArrayList = Java.type("java.util.ArrayList");
var HashSet = Java.type("java.util.HashSet");
var ArrayDeque = Java.type("java.util.ArrayDeque");

// The maximum number of nodes that can fail at once
var maxFailureCount = %failures%;
// The recovery delay in clock ticks
var moteRecoveryDelay = %delay%;
// The failure probability for a single node (1/this value)
var moteFailureProbability = 100;
var simulationStopTick = %tick%;
var terminate = false;

// Trickle params
var trickleIMin = %imin%;
var trickleIMax = %imax%;
var trickleRedundancyConst = %k%;

// The random generator for failing motes (tied to the sim seed)
var rng = new Random(sim.getRandomSeed());
// The failure mode for this simulation
var failureMode = "%mode%";

// Simulation file output object
var outputs = new Object();

// Simulation log file prefix
var filePrefix = "run_" + runNumber + "_" + maxFailureCount + "fail_" + trickleIMin + "-" + trickleIMax + "-" +
    trickleRedundancyConst+ "_" + failureMode + "_log_";

var allMotes = sim.getMotes();
var failedMotes = new ArrayList(maxFailureCount);
var failedMotesTime = new ArrayList(maxFailureCount);

// The source and sink node IDs - they cannot be the same.
var sourceMoteID = Math.floor(rng.nextFloat() * allMotes.length);
var sinkMoteID = Math.floor(rng.nextFloat() * allMotes.length);
var sourceMessageLimit = 1;

// Make sure that the source and sink aren't the same
while (sourceMoteID === sinkMoteID) {
    sinkMoteID = Math.floor(rng.nextFloat() * allMotes.length);
}

// Actually get the mote(s) and tell them that they're these nodes
var sourceMote = allMotes[sourceMoteID];
var sinkMote = allMotes[sinkMoteID];

log.log("src: " + sourceMote + "\n");
log.log("snk: " + sinkMote + "\n");

// Create a failable motes array
var failableMotes = new ArrayList(allMotes.length - 2);
for (var i = 0; i < allMotes.length; i++) {
    if (i !== sourceMoteID && i !== sinkMoteID) {
        failableMotes.add(allMotes[i]);
    }
}

// Temporal failure mode-only variables
var temporalProbability = 33;
var temporalCrashDelay = 500;

// Create the NodeGraph instance
var nodeGraph = new NodeGraph(sim);

// Initialise the outputs
for each (var m in allMotes) outputs[m.getID().toString()] = new FileWriter(filePrefix + m.getID().toString() + ".txt");

GENERATE_MSG(2000, "start-sim");
YIELD_THEN_WAIT_UNTIL(msg.equals("start-sim"));

write(sourceMote, "set source");
log.log("Initialising source with limit: " + sourceMessageLimit + "\n");
write(sourceMote, "limit " + sourceMessageLimit);
write(sinkMote, "set sink");

log.log("Initialising sim with imin: " + trickleIMin + " imax: " + trickleIMax + " redundancy cost: " +
    trickleRedundancyConst + "\n");
for each (var m in allMotes) write(m, "init " + trickleIMin + " " + trickleIMax + " " + trickleRedundancyConst);

// If there are motes to fail, the sim probably should be cut short
if (maxFailureCount > 0) {
    TIMEOUT(%timeout%, log.log("\n\nSimulation time out\n"));
}

/**
 * A high-level function to fail node(s) in the simulation based on a specified failure mode
 * @param failureMode - The failure mode (String) to determine how to fail node(s).
 */
function failNode(failureMode) {
    // Check if we can fail any more nodes and return early if not
    if ((maxFailureCount - failedMotes.length) <= 0) {
        return;
    }

    var moteToFail;
    if (failureMode === "location") {
        // Get a list of candidate motes to fail
        var moteList = [];
        if (failedMotes.length > 0) {
            // Get a list of all 1-hop neighbours for all of the failed motes
            // Duplicates don't matter since set operations later will clean them up
            failedMotes.forEach(function(elem) {
                moteList.concat(nodeGraph.get1HopNeighbours(elem));
            });
        } else {
            moteList = failableMotes;
        }

        // Filter out any motes that have are already failed
        var moteSet = new HashSet(moteList);
        moteSet.removeAll(new HashSet(failedMotes));

        // Cannot fail any more motes since the failable motes are all currently offline
        if (moteSet.isEmpty()) {
            return;
        }

        // Get a mote from the set
        var choice = rng.nextInt(moteSet.size());
        var moteSetList = new ArrayList(moteSet);
        moteToFail = moteSetList.get(choice);
    } else {
        moteToFail = failableMotes.get(rng.nextInt(failableMotes.size()));
    }

    // Check that the mote selected will not break sim constraints
    nodeGraph.toggleMote(moteToFail);
    if (!nodeGraph.isConnected()) {
        // Toggle it a 2nd time since the constraints are broken
        nodeGraph.toggleMote(moteToFail);
        return;
    }
    log.log("Failing mote " + moteToFail + "\n");

    // Restart the mote
    var timeOfRestart = time + moteRecoveryDelay;
    failedMotes.add(moteToFail);
    failedMotesTime.add(timeOfRestart);
    write(moteToFail, "sleep " + moteRecoveryDelay);
}

var consistentSet = new HashSet(allMotes.length);

while (true) {
    outputs[mote.getID().toString()].write(time + ";" + msg + "\n");

    try {
        YIELD();

        for (var i = 0; i < failedMotes.size(); i++) {
            // If the node needs to be brought back online
            if (time >= failedMotesTime.get(i)) {
                // Remove the mote from the failed motes list -- wakeup is done on the mote
                nodeGraph.toggleMote(failedMotes.get(i));
                log.log("Mote " + failedMotes.get(i) + " is online\n");
                failedMotesTime.remove(i);
                failedMotes.remove(i);
            }
        }

        if (failureMode === "temporal") {
            // TODO
        } else {
            if (rng.nextInt(moteFailureProbability) === 0) {
                failNode(failureMode);
            }
        }

        // If there are no crashes, measure the time it takes to complete the sim
        if (maxFailureCount === 0) {
            // If the message is a consistency report then add it to the list of nodes with that message
            if (msg.indexOf('Consistent') !== -1) {
                consistentSet.add(id);
            }

            // If all motes have reported consistency, report the time and end the sim
            if (consistentSet.size() === allMotes.length) {
                for (var ids in outputs) {
                    log.log("Closing filewriter for id " + ids + "\n");
                    outputs[ids].close();
                }

                log.log(time + " all motes converged, closing sim\n");

                testOK();
            }
        } else {
            if (maxFailureCount > 0 && simulationStopTick > 0 && time >= simulationStopTick && terminate === false) {
                for each (var m in allMotes) write(m, "print");
                terminate = true;
            }

            if (msg.indexOf("Current token") !== -1) {
                consistentSet.add(id);
            }

            if (consistentSet.size() === allMotes.length) {
                for (var ids in outputs) {
                    log.log("Closing filewriter for id " + ids + "\n");
                    outputs[ids].close();
                }

                log.log(time + " all motes output token, closing sim\n");

                testOK();
            }
        }
    } catch (e) {
        // If the sim is supposed to cut short (with mote failures)
       for (var ids in outputs) {
            log.log("Closing filewriter for id " + ids + "\n");
            outputs[ids].close();
        }

        throw("Simulation script killed");
    }
}
