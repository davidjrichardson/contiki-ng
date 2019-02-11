/**
 * TPWSN Trickle Simulation Script
 * 
 * Created by David Richardson, University of Warwick
 */
load("nashorn:mozilla_compat.js");
importPackage(java.io);
importPackage(java.util);
importPackage(org.contikios.cooja.util);
importPackage(org.contikios.cooja.interfaces);

// Java types
var ArrayList = Java.type("java.util.ArrayList");
var HashSet = Java.type("java.util.HashSet");
var ArrayDeque = Java.type("java.util.ArrayDeque");

// The maximum number of nodes that can fail at once
var maxFailureCount = 1;
// The recovery delay in clock ticks
var moteRecoveryDelay = 5000;
// The failure probability for a single node (1/this value)
var moteFailureProbability = 100;

// The random generator for failing motes (tied to the sim seed)
var random = new Random(sim.getRandomSeed());
// The failure mode for this simulation
var failureMode = "random";

// Simulation file output object
var outputs = new Object();
// Simulation log file prefix
var filePrefix = "log_";

var allMotes = sim.getMotes();
var failedMotes = new ArrayList(maxFailureCount);
var failedMotesTime = new ArrayList(maxFailureCount);

// The source and sink node IDs - they cannot be the same.
var sourceMoteID = Math.floor(Math.random() * allMotes.length);
var sinkMoteID = Math.floor(Math.random() * allMotes.length);
var sourceMessageLimit = 1;

// Make sure that the source and sink aren't the same
while (sourceMoteID === sinkMoteID) {
   sinkMoteID = Math.floor(Math.random() * allMotes.length);
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

GENERATE_MSG(2000, "start-sim");
YIELD_THEN_WAIT_UNTIL(msg.equals("start-sim"));

write(sourceMote, "set source");
log.log("Initialising source with limit: " + sourceMessageLimit + "\n");
write(sourceMote, "limit " + sourceMessageLimit);
write(sinkMote, "set sink");

var trickleIMin = 16;
var trickleIMax = 10;
var trickleRedundancyConst = 2;

log.log("Initialising sim with imin: " + trickleIMin + " imax: " + trickleIMax + " redundancy cost: " +
    trickleRedundancyConst + "\n");
for each (var m in allMotes) write(m, "init " + trickleIMin + " " + trickleIMax + " " + trickleRedundancyConst);

// TODO: Remove this timeout
TIMEOUT(100000, log.log("\n\nfoo\n"));

/**
 * A high-level function to fail node(s) in the simulation based on a specified failure mode
 * @param failureMode - The failure mode (String) to determine how to fail node(s).
 */
function failNode(failureMode) {
    // Check if we can fail any more nodes and return early if not
    if ((maxFailureCount - failedMotes.length) <= 0) {
        return;
    }

    // TODO: When a failure occurs:
    // * Choose a set of nodes that are relevant to the failure mode
    // --> Location based: Pick from 1-hop neighbourhood of failed node (or random if none)
    // --> Temporal: Pick a node to fail at random
    // * Figure out if the failure would break network constraint(s)
    // --> Check if the neighbourhood of the node is going to partition
    // --> Check if there are any more nodes that are allowed to fail
    // * Choose node(s) to fail based on failure mode and number of nodes that are allowed to fail
    // --> If the failure mode is temporal, another failure should happen shortly

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
        var choice = random.nextInt(moteSet.size());
        var moteSetList = new ArrayList(moteSet);
        moteToFail = moteSetList.get(choice);
    } else {
        moteToFail = failableMotes.get(random.nextInt(failableMotes.size()));
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

while (true) {
    // TODO: This is where the main sim loop sits
    // Need to update and track the failed mote(s), as well as write the mote output
    // to file(s)

    // Write the output for each mote to a file
    if (!outputs[id.toString()]) {
        log.log("Opening file for id " + id.toString() + "\n");
        outputs[id.toString()] = new FileWriter(filePrefix + id + ".txt");
    }
    
    outputs[id.toString()].write(time + ";" + msg + "\n");

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
            // if (!failedMotes.isEmpty() && current) {
            //     if (random.nextInt(temporalCrashDelay) === 0) {
            //         failNode(failureMode);
            //     }
            // }
        } else {
            if (random.nextInt(moteFailureProbability) === 0) {
                failNode(failureMode);
            }
        }
    } catch (e) {
        for (var ids in outputs) {
            log.log("Closing filewriter for id " + ids + "\n");
            outputs[ids].close();
        }

        throw("Simulation script killed")
    }
}
