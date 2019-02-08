/**
 * TPWSN Trickle Simulation Script
 * 
 * Created by David Richardson, University of Warwick
 */
load("nashorn:mozilla_compat.js");
importPackage(java.io);
importPackage(java.util);
importPackage(org.contikios.cooja.util);

// Java types
var ArrayList = Java.type("java.util.ArrayList");
var HashSet = Java.type("java.util.HashSet");

// The maximum number of nodes that can fail at once
var maxFailureCount = 1;
// The recovery delay in clock ticks
var moteRecoveryDelay = 500;
// The failure probability for a single node (1/this value)
var moteFailureProbability = 100;

// The random generator for failing motes (tied to the sim seed)
var random = new Random(sim.getRandomSeed());
// The failure mode for this simulation
var failureMode = "location";

// Simulation file output object
var outputs = new Object();
// Simulation log file prefix
var filePrefix = "log_";

var allMotes = sim.getMotes();
var failedMotes = new ArrayList(maxFailureCount);

// The source and sink node IDs - they cannot be the same.
var sourceMoteID = Math.floor(Math.random() * allMotes.length);
var sinkMoteID = Math.floor(Math.random() * allMotes.length);

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
        failableMotes.push(allMotes[i]);
    }
}

// Create the NodeGraph instance
var nodeGraph = new NodeGraph(sim);

GENERATE_MSG(2000, "start-sim");
YIELD_THEN_WAIT_UNTIL(msg.equals("start-sim"));

write(sourceMote, "set source");
write(sourceMote, "limit 1");
write(sinkMote, "set sink");

TIMEOUT(10000, log.log("\n\nfoo\n"));

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

    // TODO: Log which mote(s) are being failed
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
    } else if (failureMode === "temporal") {

        // TODO: Pick a note at random and schedule another failure (with increased likelihood)
    } else if (failureMode === "random") {
        moteToFail = failableMotes.get(random.nextInt(failableMotes.size()));
    }
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

        if (random.nextInt(moteFailureProbability) === 0) {
            failNode(failureMode);

            // TODO: Set up a timer if the failure mode is temporal
        }
    } catch (e) {
        for (var ids in outputs) {
            log.log("Closing filewriter for id " + ids + "\n");
            outputs[ids].close();
        }

        throw("Simulation script killed")
    }
}
