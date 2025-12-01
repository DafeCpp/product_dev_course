import { Routes, Route } from 'react-router-dom'
import Layout from './components/Layout'
import ExperimentsList from './pages/ExperimentsList'
import ExperimentDetail from './pages/ExperimentDetail'
import CreateExperiment from './pages/CreateExperiment'
import RunDetail from './pages/RunDetail'
import './App.css'

function App() {
  return (
    <Layout>
      <Routes>
        <Route path="/" element={<ExperimentsList />} />
        <Route path="/experiments" element={<ExperimentsList />} />
        <Route path="/experiments/new" element={<CreateExperiment />} />
        <Route path="/experiments/:id" element={<ExperimentDetail />} />
        <Route path="/runs/:id" element={<RunDetail />} />
      </Routes>
    </Layout>
  )
}

export default App

