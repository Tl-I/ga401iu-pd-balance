public static class ChargingRibbonPolicy {
    public static string Next(int previous, int current) {
        if (current == previous) return null;
        return current == 2 ? "pd" : current == 1 || current == 3 ? "barrel" : null;
    }
}
